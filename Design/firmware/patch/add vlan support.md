Add support for vlanaware bridge. We reserve vid 4095 for standalone
mode, to implement fdb-isolation. An vlan-unaware bridge uses vid 0.

Reviewed-by: Steen Hegelund <Steen.Hegelund@xxxxxxxxxxxxx>
Signed-off-by: Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
---
 drivers/net/dsa/microchip/lan9645x/Makefile        |   1 +
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.c |  63 ++++
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.h |  20 ++
 drivers/net/dsa/microchip/lan9645x/lan9645x_port.c |   3 +
 drivers/net/dsa/microchip/lan9645x/lan9645x_vlan.c | 339 +++++++++++++++++++++
 5 files changed, 426 insertions(+)

diff --git a/drivers/net/dsa/microchip/lan9645x/Makefile b/drivers/net/dsa/microchip/lan9645x/Makefile
index eea1edc5c0e3..bb5eec14d225 100644
--- a/drivers/net/dsa/microchip/lan9645x/Makefile
+++ b/drivers/net/dsa/microchip/lan9645x/Makefile
@@ -5,3 +5,4 @@ mchp-lan9645x-objs := lan9645x_main.o \
 	lan9645x_npi.o \
 	lan9645x_port.o \
 	lan9645x_phylink.o \
+	lan9645x_vlan.o \
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
index b6efaf669a3f..1c8f20452487 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
@@ -172,6 +172,7 @@ static int lan9645x_setup(struct dsa_switch *ds)
 	}
 
 	mutex_init(&lan9645x->fwd_domain_lock);
+	lan9645x_vlan_init(lan9645x);
 
 	/* Link Aggregation Mode: NETDEV_LAG_HASH_L2 */
 	lan_wr(ANA_AGGR_CFG_AC_SMAC_ENA |
@@ -471,11 +472,68 @@ static void lan9645x_port_bridge_leave(struct dsa_switch *ds, int port,
 	if (!lan9645x->bridge_mask)
 		lan9645x->bridge = NULL;
 
+	lan9645x_vlan_set_hostmode(p);
 	lan9645x_update_fwd_mask(lan9645x);
 
 	mutex_unlock(&lan9645x->fwd_domain_lock);
 }
 
+static int lan9645x_port_vlan_filtering(struct dsa_switch *ds, int port,
+					bool enabled,
+					struct netlink_ext_ack *extack)
+{
+	struct lan9645x *lan9645x = ds->priv;
+	struct lan9645x_port *p;
+
+	p = lan9645x_to_port(lan9645x, port);
+	lan9645x_vlan_port_set_vlan_aware(p, enabled);
+	lan9645x_vlan_port_apply(p);
+
+	return 0;
+}
+
+static int lan9645x_port_vlan_add(struct dsa_switch *ds, int port,
+				  const struct switchdev_obj_port_vlan *vlan,
+				  struct netlink_ext_ack *extack)
+{
+	struct lan9645x *lan9645x = ds->priv;
+	struct lan9645x_port *p;
+	bool pvid, untagged;
+	int err;
+
+	p = lan9645x_to_port(lan9645x, port);
+
+	pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
+	untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
+
+	err = lan9645x_port_vlan_prepare(p, vlan->vid, pvid, untagged, extack);
+	if (err)
+		return err;
+
+	if (port == lan9645x->npi)
+		lan9645x_vlan_cpu_set_vlan(lan9645x, vlan->vid);
+
+	lan9645x_vlan_port_add_vlan(p, vlan->vid, pvid, untagged);
+
+	return 0;
+}
+
+static int lan9645x_port_vlan_del(struct dsa_switch *ds, int port,
+				  const struct switchdev_obj_port_vlan *vlan)
+{
+	struct lan9645x *lan9645x = ds->priv;
+	struct lan9645x_port *p;
+
+	p = lan9645x_to_port(lan9645x, port);
+
+	if (port == lan9645x->npi)
+		lan9645x_vlan_cpu_clear_vlan(lan9645x, vlan->vid);
+
+	lan9645x_vlan_port_del_vlan(p, vlan->vid);
+
+	return 0;
+}
+
 static const struct dsa_switch_ops lan9645x_switch_ops = {
 	.get_tag_protocol		= lan9645x_get_tag_protocol,
 	.connect_tag_protocol		= lan9645x_connect_tag_protocol,
@@ -497,6 +555,11 @@ static const struct dsa_switch_ops lan9645x_switch_ops = {
 	.port_bridge_join		= lan9645x_port_bridge_join,
 	.port_bridge_leave		= lan9645x_port_bridge_leave,
 	.port_stp_state_set		= lan9645x_port_bridge_stp_state_set,
+
+	/* VLAN integration */
+	.port_vlan_filtering		= lan9645x_port_vlan_filtering,
+	.port_vlan_add			= lan9645x_port_vlan_add,
+	.port_vlan_del			= lan9645x_port_vlan_del,
 };
 
 static int lan9645x_request_target_regmaps(struct lan9645x *lan9645x)
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
index bf110bdbc90c..f1471344a9e9 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
@@ -181,6 +181,11 @@ struct lan9645x {
 	u16 bridge_fwd_mask; /* Mask for forwarding bridged ports */
 	struct mutex fwd_domain_lock; /* lock forwarding configuration */
 
+	/* VLAN */
+	u16 vlan_mask[VLAN_N_VID]; /* Port mask per vlan */
+	u8 vlan_flags[VLAN_N_VID];
+	DECLARE_BITMAP(cpu_vlan_mask, VLAN_N_VID); /* CPU VLAN membership */
+
 	int num_port_dis;
 	bool dd_dis;
 	bool tsn_dis;
@@ -404,4 +409,19 @@ void lan9645x_phylink_get_caps(struct lan9645x *lan9645x, int port,
 			       struct phylink_config *c);
 void lan9645x_phylink_port_down(struct lan9645x *lan9645x, int port);
 
+/* VLAN lan9645x_vlan.c */
+void lan9645x_vlan_init(struct lan9645x *lan9645x);
+void lan9645x_vlan_port_set_vlan_aware(struct lan9645x_port *p,
+				       bool vlan_aware);
+u16 lan9645x_vlan_unaware_pvid(bool is_bridged);
+void lan9645x_vlan_port_apply(struct lan9645x_port *p);
+void lan9645x_vlan_port_add_vlan(struct lan9645x_port *p, u16 vid, bool pvid,
+				 bool untagged);
+void lan9645x_vlan_port_del_vlan(struct lan9645x_port *p, u16 vid);
+void lan9645x_vlan_cpu_set_vlan(struct lan9645x *lan9645x, u16 vid);
+void lan9645x_vlan_cpu_clear_vlan(struct lan9645x *lan9645x, u16 vid);
+void lan9645x_vlan_set_hostmode(struct lan9645x_port *p);
+int lan9645x_port_vlan_prepare(struct lan9645x_port *p, u16 vid, bool pvid,
+			       bool untagged, struct netlink_ext_ack *extack);
+
 #endif /* __LAN9645X_MAIN_H__ */
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
index b60c64458957..d69018b015b5 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_port.c
@@ -44,6 +44,9 @@ int lan9645x_port_init(struct lan9645x *lan9645x, int port)
 		ANA_PORT_CFG_PORTID_VAL,
 		lan9645x, ANA_PORT_CFG(p->chip_port));
 
+	if (p->chip_port != lan9645x->npi)
+		lan9645x_vlan_set_hostmode(p);
+
 	return 0;
 }
 
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_vlan.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_vlan.c
new file mode 100644
index 000000000000..1fc2a0ffe3b1
--- /dev/null
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_vlan.c
@@ -0,0 +1,339 @@
+// SPDX-License-Identifier: GPL-2.0+
+/* Copyright (C) 2026 Microchip Technology Inc.
+ */
+
+#include "lan9645x_main.h"
+
+#define VLANACCESS_CMD_IDLE		0
+#define VLANACCESS_CMD_READ		1
+#define VLANACCESS_CMD_WRITE		2
+#define VLANACCESS_CMD_INIT		3
+
+int lan9645x_port_vlan_prepare(struct lan9645x_port *p, u16 vid, bool pvid,
+			       bool untagged, struct netlink_ext_ack *extack)
+{
+	struct lan9645x *lan9645x = p->lan9645x;
+
+	if (p->chip_port == lan9645x->npi)
+		return 0;
+
+	if (untagged && p->untagged_vid && p->untagged_vid != vid) {
+		NL_SET_ERR_MSG_MOD(extack,
+				   "Only one egress-untagged VLAN allowed.");
+		return -EBUSY;
+	}
+
+	if (vid > VLAN_MAX) {
+		NL_SET_ERR_MSG_MOD(extack, "VLAN range 4094-4095 reserved.");
+		return -EBUSY;
+	}
+
+	return 0;
+}
+
+static int lan9645x_vlan_wait_for_completion(struct lan9645x *lan9645x)
+{
+	u32 val;
+
+	return lan9645x_rd_poll_timeout(lan9645x, ANA_VLANACCESS, val,
+					ANA_VLANACCESS_VLAN_TBL_CMD_GET(val) ==
+					VLANACCESS_CMD_IDLE);
+}
+
+static void lan9645x_vlan_set_mask(struct lan9645x *lan9645x, u16 vid)
+{
+	u16 mask = lan9645x->vlan_mask[vid];
+	u8 f = lan9645x->vlan_flags[vid];
+	bool cpu_dis;
+	u32 val;
+
+	cpu_dis = !(mask & BIT(CPU_PORT));
+
+	/* Set flags and the VID to configure */
+	val = ANA_VLANTIDX_VLAN_PGID_CPU_DIS_SET(cpu_dis) |
+	      ANA_VLANTIDX_V_INDEX_SET(vid) |
+	      ANA_VLANTIDX_VLAN_SEC_FWD_ENA_SET(!!(f & VLAN_S_FWD_ENA)) |
+	      ANA_VLANTIDX_VLAN_FLOOD_DIS_SET(!!(f & VLAN_FLD_DIS)) |
+	      ANA_VLANTIDX_VLAN_PRIV_VLAN_SET(!!(f & VLAN_PRV_VLAN)) |
+	      ANA_VLANTIDX_VLAN_LEARN_DISABLED_SET(!!(f & VLAN_LRN_DIS)) |
+	      ANA_VLANTIDX_VLAN_MIRROR_SET(!!(f & VLAN_MIR)) |
+	      ANA_VLANTIDX_VLAN_SRC_CHK_SET(!!(f & VLAN_SRC_CHK));
+
+	lan_wr(val, lan9645x, ANA_VLANTIDX);
+
+	/* Set the vlan port members mask, which enables ingress filtering */
+	lan_wr(mask, lan9645x, ANA_VLAN_PORT_MASK);
+
+	/* Issue a write command */
+	lan_wr(VLANACCESS_CMD_WRITE, lan9645x, ANA_VLANACCESS);
+
+	if (lan9645x_vlan_wait_for_completion(lan9645x))
+		dev_err(lan9645x->dev, "Vlan set mask failed\n");
+}
+
+static void lan9645x_vlan_port_add_vlan_mask(struct lan9645x_port *p, u16 vid)
+{
+	struct lan9645x *lan9645x = p->lan9645x;
+
+	lan9645x->vlan_mask[vid] |= BIT(p->chip_port);
+	lan9645x_vlan_set_mask(lan9645x, vid);
+}
+
+static void lan9645x_vlan_port_del_vlan_mask(struct lan9645x_port *p, u16 vid)
+{
+	struct lan9645x *lan9645x = p->lan9645x;
+
+	lan9645x->vlan_mask[vid] &= ~BIT(p->chip_port);
+	lan9645x_vlan_set_mask(lan9645x, vid);
+}
+
+static void lan9645x_vlan_cpu_add_vlan_mask(struct lan9645x *lan9645x, u16 vid)
+{
+	lan9645x->vlan_mask[vid] |= BIT(CPU_PORT);
+	lan9645x_vlan_set_mask(lan9645x, vid);
+}
+
+static void lan9645x_vlan_cpu_del_vlan_mask(struct lan9645x *lan9645x, u16 vid)
+{
+	lan9645x->vlan_mask[vid] &= ~BIT(CPU_PORT);
+	lan9645x_vlan_set_mask(lan9645x, vid);
+}
+
+static bool lan9645x_vlan_port_any_vlan_mask(struct lan9645x *lan9645x, u16 vid)
+{
+	return !!(lan9645x->vlan_mask[vid] & ~BIT(CPU_PORT));
+}
+
+void lan9645x_vlan_cpu_set_vlan(struct lan9645x *lan9645x, u16 vid)
+{
+	__set_bit(vid, lan9645x->cpu_vlan_mask);
+}
+
+void lan9645x_vlan_cpu_clear_vlan(struct lan9645x *lan9645x, u16 vid)
+{
+	__clear_bit(vid, lan9645x->cpu_vlan_mask);
+}
+
+static bool lan9645x_vlan_cpu_member_cpu_vlan_mask(struct lan9645x *lan9645x,
+						   u16 vid)
+{
+	return test_bit(vid, lan9645x->cpu_vlan_mask);
+}
+
+u16 lan9645x_vlan_unaware_pvid(bool is_bridged)
+{
+	return is_bridged ? UNAWARE_PVID : HOST_PVID;
+}
+
+static u16 lan9645x_vlan_port_get_pvid(struct lan9645x_port *port)
+{
+	bool is_bridged = lan9645x_port_is_bridged(port);
+
+	if (is_bridged && port->vlan_aware)
+		return port->pvid;
+	else
+		return lan9645x_vlan_unaware_pvid(is_bridged);
+}
+
+static void lan9645x_vlan_port_set_vid(struct lan9645x_port *p, u16 vid,
+				       bool pvid, bool untagged)
+{
+	/* Egress vlan classification */
+	if (untagged)
+		p->untagged_vid = vid;
+
+	/* Default ingress vlan classification */
+	if (pvid)
+		p->pvid = vid;
+}
+
+static void lan9645x_vlan_port_remove_vid(struct lan9645x_port *p, u16 vid)
+{
+	if (p->pvid == vid)
+		p->pvid = 0;
+
+	if (p->untagged_vid == vid)
+		p->untagged_vid = 0;
+}
+
+void lan9645x_vlan_port_set_vlan_aware(struct lan9645x_port *p,
+				       bool vlan_aware)
+{
+	p->vlan_aware = vlan_aware;
+}
+
+void lan9645x_vlan_port_apply(struct lan9645x_port *p)
+{
+	struct lan9645x *lan9645x = p->lan9645x;
+	u32 val, tag_cfg;
+	u16 pvid;
+
+	pvid = lan9645x_vlan_port_get_pvid(p);
+
+	/* Ingress classification (ANA_PORT_VLAN_CFG) */
+	/* Default vlan to classify for untagged frames (may be zero) */
+	val = ANA_VLAN_CFG_VLAN_VID_SET(pvid);
+	if (p->vlan_aware)
+		val |= ANA_VLAN_CFG_VLAN_AWARE_ENA_SET(1) |
+			ANA_VLAN_CFG_VLAN_POP_CNT_SET(1);
+
+	lan_rmw(val,
+		ANA_VLAN_CFG_VLAN_VID |
+		ANA_VLAN_CFG_VLAN_AWARE_ENA |
+		ANA_VLAN_CFG_VLAN_POP_CNT,
+		lan9645x, ANA_VLAN_CFG(p->chip_port));
+
+	lan_rmw(DEV_MAC_TAGS_CFG_VLAN_AWR_ENA_SET(p->vlan_aware) |
+		DEV_MAC_TAGS_CFG_PB_ENA_SET(p->vlan_aware),
+		DEV_MAC_TAGS_CFG_VLAN_AWR_ENA |
+		DEV_MAC_TAGS_CFG_PB_ENA,
+		lan9645x, DEV_MAC_TAGS_CFG(p->chip_port));
+
+	/* Drop frames with multicast source address */
+	val = ANA_DROP_CFG_DROP_MC_SMAC_ENA_SET(1);
+	if (p->vlan_aware && !pvid)
+		/* If port is vlan-aware and tagged, drop untagged and priority
+		 * tagged frames.
+		 */
+		val |= ANA_DROP_CFG_DROP_UNTAGGED_ENA_SET(1) |
+			ANA_DROP_CFG_DROP_PRIO_S_TAGGED_ENA_SET(1) |
+			ANA_DROP_CFG_DROP_PRIO_C_TAGGED_ENA_SET(1);
+
+	lan_wr(val, lan9645x, ANA_DROP_CFG(p->chip_port));
+
+	/* TAG_TPID_CFG encoding:
+	 *
+	 * 0: Use 0x8100.
+	 * 1: Use 0x88A8.
+	 * 2: Use custom value from PORT_VLAN_CFG.PORT_TPID.
+	 * 3: Use PORT_VLAN_CFG.PORT_TPID, unless ingress tag was a C-tag
+	 *    (EtherType = 0x8100)
+	 *
+	 * Use 3 and PORT_VLAN_CFG.PORT_TPID=0x88a8 to ensure stags are not
+	 * rewritten to ctags on egress.
+	 */
+	val = REW_TAG_CFG_TAG_TPID_CFG_SET(3);
+	if (p->vlan_aware) {
+		if (p->untagged_vid)
+			tag_cfg = LAN9645X_TAG_NO_PVID_NO_UNAWARE;
+		else
+			tag_cfg = LAN9645X_TAG_ALL;
+
+		val |= REW_TAG_CFG_TAG_CFG_SET(tag_cfg);
+	}
+
+	lan_rmw(val,
+		REW_TAG_CFG_TAG_TPID_CFG |
+		REW_TAG_CFG_TAG_CFG,
+		lan9645x, REW_TAG_CFG(p->chip_port));
+
+	/* Set default VLAN and tag type to 8021Q */
+
+	lan_rmw(REW_PORT_VLAN_CFG_PORT_TPID_SET(ETH_P_8021AD) |
+		REW_PORT_VLAN_CFG_PORT_VID_SET(p->untagged_vid),
+		REW_PORT_VLAN_CFG_PORT_TPID |
+		REW_PORT_VLAN_CFG_PORT_VID,
+		lan9645x, REW_PORT_VLAN_CFG(p->chip_port));
+}
+
+void lan9645x_vlan_port_add_vlan(struct lan9645x_port *p, u16 vid, bool pvid,
+				 bool untagged)
+{
+	struct lan9645x *lan9645x = p->lan9645x;
+
+	if (lan9645x_vlan_cpu_member_cpu_vlan_mask(lan9645x, vid))
+		lan9645x_vlan_cpu_add_vlan_mask(lan9645x, vid);
+
+	lan9645x_vlan_port_set_vid(p, vid, pvid, untagged);
+	lan9645x_vlan_port_add_vlan_mask(p, vid);
+	lan9645x_vlan_port_apply(p);
+}
+
+void lan9645x_vlan_port_del_vlan(struct lan9645x_port *port, u16 vid)
+{
+	struct lan9645x *lan9645x = port->lan9645x;
+
+	lan9645x_vlan_port_remove_vid(port, vid);
+	lan9645x_vlan_port_del_vlan_mask(port, vid);
+	lan9645x_vlan_port_apply(port);
+
+	if (!lan9645x_vlan_port_any_vlan_mask(lan9645x, vid))
+		lan9645x_vlan_cpu_del_vlan_mask(lan9645x, vid);
+}
+
+static void lan9645x_vlan_port_rew_host(struct lan9645x_port *port)
+{
+	struct lan9645x *lan9645x = port->lan9645x;
+
+	lan_rmw(REW_TAG_CFG_TAG_CFG_SET(LAN9645X_TAG_NO_PVID_NO_UNAWARE) |
+		REW_TAG_CFG_TAG_TPID_CFG_SET(3),
+		REW_TAG_CFG_TAG_CFG |
+		REW_TAG_CFG_TAG_TPID_CFG,
+		lan9645x, REW_TAG_CFG(port->chip_port));
+
+	/* Standalone ports must have the reserved VID set in the rewriter,
+	 * because the TAG_CFG used above acts on this PORT_VID value, otherwise
+	 * untagged frames are tagged with HOST_PVID.
+	 *
+	 * Usually frames leaving the switch from standalone ports go to the
+	 * CPU, where tagging is disabled. But if we mirror a standalone port,
+	 * the problem becomes apparent.
+	 */
+	lan_rmw(REW_PORT_VLAN_CFG_PORT_TPID_SET(ETH_P_8021AD) |
+		REW_PORT_VLAN_CFG_PORT_VID_SET(HOST_PVID),
+		REW_PORT_VLAN_CFG_PORT_TPID |
+		REW_PORT_VLAN_CFG_PORT_VID,
+		lan9645x, REW_PORT_VLAN_CFG(port->chip_port));
+}
+
+void lan9645x_vlan_set_hostmode(struct lan9645x_port *p)
+{
+	lan9645x_vlan_port_set_vlan_aware(p, false);
+	lan9645x_vlan_port_set_vid(p, HOST_PVID, false, false);
+	lan9645x_vlan_port_apply(p);
+	lan9645x_vlan_port_rew_host(p);
+}
+
+void lan9645x_vlan_init(struct lan9645x *lan9645x)
+{
+	u32 all_phys_ports, all_ports;
+	u16 port, vid;
+
+	all_phys_ports = GENMASK(lan9645x->num_phys_ports - 1, 0);
+	all_ports = all_phys_ports | BIT(CPU_PORT);
+
+	/* Clear VLAN table, by default all ports are members of all VLANS */
+	lan_wr(ANA_VLANACCESS_VLAN_TBL_CMD_SET(VLANACCESS_CMD_INIT),
+	       lan9645x, ANA_VLANACCESS);
+
+	if (lan9645x_vlan_wait_for_completion(lan9645x))
+		dev_err(lan9645x->dev, "Vlan clear table failed\n");
+
+	for (vid = 1; vid < VLAN_N_VID; vid++) {
+		lan9645x->vlan_mask[vid] = 0;
+		lan9645x_vlan_set_mask(lan9645x, vid);
+	}
+
+	/* Set all the ports + cpu to be part of HOST_PVID and UNAWARE_PVID */
+	lan9645x->vlan_mask[HOST_PVID] = all_ports;
+	lan9645x_vlan_set_mask(lan9645x, HOST_PVID);
+
+	lan9645x->vlan_mask[UNAWARE_PVID] = all_ports;
+	lan9645x_vlan_set_mask(lan9645x, UNAWARE_PVID);
+
+	lan9645x_vlan_cpu_set_vlan(lan9645x, UNAWARE_PVID);
+
+	/* Configure the CPU port to be vlan aware */
+	lan_wr(ANA_VLAN_CFG_VLAN_VID_SET(UNAWARE_PVID) |
+	       ANA_VLAN_CFG_VLAN_AWARE_ENA_SET(1) |
+	       ANA_VLAN_CFG_VLAN_POP_CNT_SET(1),
+	       lan9645x, ANA_VLAN_CFG(CPU_PORT));
+
+	/* Set vlan ingress filter mask to all ports */
+	lan_wr(all_ports, lan9645x, ANA_VLANMASK);
+
+	for (port = 0; port < lan9645x->num_phys_ports; port++) {
+		lan_wr(0, lan9645x, REW_PORT_VLAN_CFG(port));
+		lan_wr(0, lan9645x, REW_TAG_CFG(port));
+	}
+}

-- 
2.52.0
