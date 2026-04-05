Add support for hardware offloading of the bridge. We support a single
bridge device.

Reviewed-by: Steen Hegelund <Steen.Hegelund@xxxxxxxxxxxxx>
Signed-off-by: Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
---
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.c | 196 +++++++++++++++++++++
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.h |  11 ++
 drivers/net/dsa/microchip/lan9645x/lan9645x_port.c |   2 +
 3 files changed, 209 insertions(+)

diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
index 739013f049d0..b6efaf669a3f 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
@@ -171,6 +171,8 @@ static int lan9645x_setup(struct dsa_switch *ds)
 		return err;
 	}
 
+	mutex_init(&lan9645x->fwd_domain_lock);
+
 	/* Link Aggregation Mode: NETDEV_LAG_HASH_L2 */
 	lan_wr(ANA_AGGR_CFG_AC_SMAC_ENA |
 	       ANA_AGGR_CFG_AC_DMAC_ENA,
@@ -288,6 +290,192 @@ static void lan9645x_port_phylink_get_caps(struct dsa_switch *ds, int port,
 	lan9645x_phylink_get_caps(ds->priv, port, config);
 }
 
+static int lan9645x_set_ageing_time(struct dsa_switch *ds, unsigned int msecs)
+{
+	u32 age_secs = max(1, msecs / 1000 / 2);
+	struct lan9645x *lan9645x = ds->priv;
+
+	/* Entry is must suffer two aging scans before it is removed, so an
+	 * entry is aged after 2*AGE_PERIOD, and the unit is in seconds.
+	 * An age period of 0 disables automatic aging.
+	 */
+	lan_rmw(ANA_AUTOAGE_AGE_PERIOD_SET(age_secs),
+		ANA_AUTOAGE_AGE_PERIOD,
+		lan9645x, ANA_AUTOAGE);
+	return 0;
+}
+
+static int lan9645x_port_pre_bridge_flags(struct dsa_switch *ds, int port,
+					  struct switchdev_brport_flags flags,
+					  struct netlink_ext_ack *extack)
+{
+	if (flags.mask &
+	    ~(BR_LEARNING | BR_FLOOD | BR_MCAST_FLOOD | BR_BCAST_FLOOD))
+		return -EINVAL;
+
+	return 0;
+}
+
+static void lan9645x_port_pgid_set(struct lan9645x *lan9645x, u16 pgid,
+				   int chip_port, bool enabled)
+{
+	u32 reg_msk, port_msk;
+
+	WARN_ON(chip_port > CPU_PORT);
+
+	port_msk = ANA_PGID_PGID_SET(enabled ? BIT(chip_port) : 0);
+	reg_msk = ANA_PGID_PGID_SET(BIT(chip_port));
+
+	lan_rmw(port_msk, reg_msk, lan9645x, ANA_PGID(pgid));
+}
+
+static void lan9645x_port_set_learning(struct lan9645x *lan9645x, int port,
+				       bool enabled)
+{
+	struct lan9645x_port *p;
+
+	lan_rmw(ANA_PORT_CFG_LEARN_ENA_SET(enabled), ANA_PORT_CFG_LEARN_ENA,
+		lan9645x, ANA_PORT_CFG(port));
+
+	p = lan9645x_to_port(lan9645x, port);
+	p->learn_ena = enabled;
+}
+
+static int lan9645x_port_bridge_flags(struct dsa_switch *ds, int port,
+				      struct switchdev_brport_flags f,
+				      struct netlink_ext_ack *extack)
+{
+	struct lan9645x *l = ds->priv;
+
+	if (WARN_ON(port == l->npi))
+		return -EINVAL;
+
+	if (f.mask & BR_LEARNING)
+		lan9645x_port_set_learning(l, port, !!(f.val & BR_LEARNING));
+
+	if (f.mask & BR_FLOOD)
+		lan9645x_port_pgid_set(l, PGID_UC, port, !!(f.val & BR_FLOOD));
+
+	if (f.mask & BR_MCAST_FLOOD) {
+		bool ena = !!(f.val & BR_MCAST_FLOOD);
+
+		lan9645x_port_pgid_set(l, PGID_MC, port, ena);
+		lan9645x_port_pgid_set(l, PGID_MCIPV4, port, ena);
+		lan9645x_port_pgid_set(l, PGID_MCIPV6, port, ena);
+	}
+
+	if (f.mask & BR_BCAST_FLOOD)
+		lan9645x_port_pgid_set(l, PGID_BC, port,
+				       !!(f.val & BR_BCAST_FLOOD));
+
+	return 0;
+}
+
+static void lan9645x_update_fwd_mask(struct lan9645x *lan9645x)
+{
+	struct lan9645x_port *p;
+	int port;
+
+	lockdep_assert_held(&lan9645x->fwd_domain_lock);
+
+	/* Updates the source port PGIDs, making sure frames from p
+	 * are only forwarded to ports q != p, where q is relevant to forward
+	 */
+	lan9645x_for_each_chipport(lan9645x, port) {
+		u32 mask = 0;
+
+		p = lan9645x_to_port(lan9645x, port);
+
+		if (lan9645x_port_is_bridged(p)) {
+			mask = lan9645x->bridge_mask &
+			       lan9645x->bridge_fwd_mask & ~BIT(p->chip_port);
+		}
+
+		lan_wr(mask, lan9645x, ANA_PGID(PGID_SRC + port));
+	}
+}
+
+static int lan9645x_port_bridge_join(struct dsa_switch *ds, int port,
+				     struct dsa_bridge bridge,
+				     bool *tx_fwd_offload,
+				     struct netlink_ext_ack *extack)
+{
+	struct lan9645x *lan9645x = ds->priv;
+	struct lan9645x_port *p;
+
+	p = lan9645x_to_port(lan9645x, port);
+
+	if (lan9645x->bridge && lan9645x->bridge != bridge.dev) {
+		NL_SET_ERR_MSG_MOD(extack, "Only one bridge supported");
+		return -EBUSY;
+	}
+
+	mutex_lock(&lan9645x->fwd_domain_lock);
+	/* First bridged port sets bridge dev */
+	if (!lan9645x->bridge_mask)
+		lan9645x->bridge = bridge.dev;
+
+	lan9645x->bridge_mask |= BIT(p->chip_port);
+	mutex_unlock(&lan9645x->fwd_domain_lock);
+
+	/* Later: stp_state_set updates forwarding */
+
+	return 0;
+}
+
+static void lan9645x_port_stp_state_set(struct lan9645x *lan9645x, int port,
+					u8 state)
+{
+	struct lan9645x_port *p = lan9645x_to_port(lan9645x, port);
+	bool learn_ena;
+
+	mutex_lock(&lan9645x->fwd_domain_lock);
+
+	p->stp_state = state;
+
+	if (state == BR_STATE_FORWARDING)
+		lan9645x->bridge_fwd_mask |= BIT(p->chip_port);
+	else
+		lan9645x->bridge_fwd_mask &= ~BIT(p->chip_port);
+
+	learn_ena = (state == BR_STATE_LEARNING ||
+		     state == BR_STATE_FORWARDING) && p->learn_ena;
+
+	lan_rmw(ANA_PORT_CFG_LEARN_ENA_SET(learn_ena),
+		ANA_PORT_CFG_LEARN_ENA, lan9645x,
+		ANA_PORT_CFG(p->chip_port));
+
+	lan9645x_update_fwd_mask(lan9645x);
+	mutex_unlock(&lan9645x->fwd_domain_lock);
+}
+
+static void lan9645x_port_bridge_stp_state_set(struct dsa_switch *ds, int port,
+					       u8 state)
+{
+	lan9645x_port_stp_state_set(ds->priv, port, state);
+}
+
+static void lan9645x_port_bridge_leave(struct dsa_switch *ds, int port,
+				       struct dsa_bridge bridge)
+{
+	struct lan9645x *lan9645x = ds->priv;
+	struct lan9645x_port *p;
+
+	p = lan9645x_to_port(lan9645x, port);
+
+	mutex_lock(&lan9645x->fwd_domain_lock);
+
+	lan9645x->bridge_mask &= ~BIT(p->chip_port);
+
+	/* Last port leaving clears bridge dev */
+	if (!lan9645x->bridge_mask)
+		lan9645x->bridge = NULL;
+
+	lan9645x_update_fwd_mask(lan9645x);
+
+	mutex_unlock(&lan9645x->fwd_domain_lock);
+}
+
 static const struct dsa_switch_ops lan9645x_switch_ops = {
 	.get_tag_protocol		= lan9645x_get_tag_protocol,
 	.connect_tag_protocol		= lan9645x_connect_tag_protocol,
@@ -301,6 +489,14 @@ static const struct dsa_switch_ops lan9645x_switch_ops = {
 	/* MTU  */
 	.port_change_mtu		= lan9645x_change_mtu,
 	.port_max_mtu			= lan9645x_get_max_mtu,
+
+	/* Bridge integration */
+	.set_ageing_time		= lan9645x_set_ageing_time,
+	.port_pre_bridge_flags		= lan9645x_port_pre_bridge_flags,
+	.port_bridge_flags		= lan9645x_port_bridge_flags,
+	.port_bridge_join		= lan9645x_port_bridge_join,
+	.port_bridge_leave		= lan9645x_port_bridge_leave,
+	.port_stp_state_set		= lan9645x_port_bridge_stp_state_set,
 };
 
 static int lan9645x_request_target_regmaps(struct lan9645x *lan9645x)
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
index a51b637f28bf..bf110bdbc90c 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
@@ -175,6 +175,12 @@ struct lan9645x {
 	/* debugfs */
 	struct dentry *debugfs_root;
 
+	/* Forwarding Database */
+	struct net_device *bridge; /* Only support single bridge */
+	u16 bridge_mask; /* Mask for bridged ports */
+	u16 bridge_fwd_mask; /* Mask for forwarding bridged ports */
+	struct mutex fwd_domain_lock; /* lock forwarding configuration */
+
 	int num_port_dis;
 	bool dd_dis;
 	bool tsn_dis;
@@ -273,6 +279,11 @@ lan9645x_chipport_to_ndev(struct lan9645x *lan9645x, int port)
 	return lan9645x_port_to_ndev(lan9645x_to_port(lan9645x, port));
 }
 
+static inline bool lan9645x_port_is_bridged(struct lan9645x_port *p)
+{
+	return p && (p->lan9645x->bridge_mask & BIT(p->chip_port));
+}
+
 static inline bool lan9645x_port_is_used(struct lan9645x *lan9645x, int port)
 {
 	struct dsa_port *dp;
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
index 038868ae0a32..b60c64458957 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
@@ -15,6 +15,8 @@ int lan9645x_port_init(struct lan9645x *lan9645x, int port)
 		ANA_PORT_CFG_LEARN_ENA,
 		lan9645x, ANA_PORT_CFG(p->chip_port));
 
+	p->learn_ena = false;
+
 	lan9645x_port_set_maxlen(lan9645x, port, ETH_DATA_LEN);
 
 	lan9645x_phylink_port_down(lan9645x, port);

-- 
2.52.0