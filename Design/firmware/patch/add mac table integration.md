Add MAC table support, and dsa fdb callback integration. The mactable is
keyed on (vid,mac) and each bucket has 4 slots. A mac table entry
typically points to a PGID index, the first 9 of which represent a front
port.

Mac table entries for L2 multicast will use a PGID containing a group
port mask. For IP multicast entries in the mac table a trick us used,
where the group port mask is packed into the MAC data, exploiting the
fact that the top bits are fixed, and that the number of switch ports is
small enough to fit in the redundant bits.

Therefore, we can avoid using sparse PGID resources for IP multicast
entries in the mac table.

Reviewed-by: Steen Hegelund <Steen.Hegelund@xxxxxxxxxxxxx>
Signed-off-by: Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
---
 drivers/net/dsa/microchip/lan9645x/Makefile        |   1 +
 drivers/net/dsa/microchip/lan9645x/lan9645x_mac.c  | 413 +++++++++++++++++++++
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.c | 110 +++++-
 drivers/net/dsa/microchip/lan9645x/lan9645x_main.h |  48 +++
 4 files changed, 571 insertions(+), 1 deletion(-)

diff --git a/drivers/net/dsa/microchip/lan9645x/Makefile b/drivers/net/dsa/microchip/lan9645x/Makefile
index bb5eec14d225..a90a46f81c72 100644
--- a/drivers/net/dsa/microchip/lan9645x/Makefile
+++ b/drivers/net/dsa/microchip/lan9645x/Makefile
@@ -6,3 +6,4 @@ mchp-lan9645x-objs := lan9645x_main.o \
 	lan9645x_port.o \
 	lan9645x_phylink.o \
 	lan9645x_vlan.o \
+	lan9645x_mac.o \
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_mac.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_mac.c
new file mode 100644
index 000000000000..3226cff16e8c
--- /dev/null
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_mac.c
@@ -0,0 +1,413 @@
+// SPDX-License-Identifier: GPL-2.0+
+/* Copyright (C) 2026 Microchip Technology Inc.
+ */
+
+#include "lan9645x_main.h"
+
+#define LAN9645X_MAC_COLUMNS	4
+
+#define CMD_IDLE		0
+#define CMD_LEARN		1
+#define CMD_FORGET		2
+#define CMD_AGE			3
+#define CMD_GET_NEXT		4
+#define CMD_INIT		5
+#define CMD_READ		6
+#define CMD_WRITE		7
+#define CMD_SYNC_GET_NEXT	8
+
+#define LAN9645X_INVALID_ROW	(-1)
+
+static bool lan9645x_mact_entry_equal(struct lan9645x_mact_entry *entry,
+				      const unsigned char *mac, u16 vid)
+{
+	/* The hardware table is keyed on (vid,mac) */
+	return entry->common.key.vid == vid &&
+		ether_addr_equal(mac, entry->common.key.mac);
+}
+
+static struct lan9645x_mact_entry *
+lan9645x_mact_entry_find(struct lan9645x *lan9645x, const unsigned char *mac,
+			 u16 vid)
+{
+	struct lan9645x_mact_entry *entry;
+
+	lockdep_assert_held(&lan9645x->mac_entry_lock);
+
+	list_for_each_entry(entry, &lan9645x->mac_entries, list)
+		if (lan9645x_mact_entry_equal(entry, mac, vid))
+			return entry;
+
+	return NULL;
+}
+
+static struct lan9645x_mact_entry *
+lan9645x_mact_entry_alloc(struct lan9645x *lan9645x, const unsigned char *mac,
+			  u16 vid, u8 pgid, enum macaccess_entry_type type)
+{
+	struct lan9645x_mact_entry *entry;
+
+	entry = kzalloc_obj(*entry);
+	if (!entry)
+		return NULL;
+
+	INIT_LIST_HEAD(&entry->list);
+	ether_addr_copy(entry->common.key.mac, mac);
+	entry->common.key.vid = vid;
+	entry->common.pgid = pgid;
+	entry->common.row = LAN9645X_INVALID_ROW;
+	entry->common.type = type;
+
+	dev_dbg(lan9645x->dev,
+		"mact_entry_alloc mac=%pM vid=%u pgid=%u type=%d",
+		entry->common.key.mac, entry->common.key.vid,
+		entry->common.pgid, entry->common.type);
+	return entry;
+}
+
+static void lan9645x_mact_entry_dealloc(struct lan9645x *lan9645x,
+					struct lan9645x_mact_entry *entry)
+{
+	if (!entry)
+		return;
+
+	dev_dbg(lan9645x->dev,
+		"mact_entry_dealloc mac=%pM vid=%u pgid=%u type=%d",
+		entry->common.key.mac, entry->common.key.vid,
+		entry->common.pgid, entry->common.type);
+
+	list_del(&entry->list);
+	kfree(entry);
+}
+
+static int lan9645x_mac_wait_for_completion(struct lan9645x *lan9645x,
+					    u32 *maca)
+{
+	u32 val = 0;
+	int err;
+
+	lockdep_assert_held(&lan9645x->mact_lock);
+
+	err = lan9645x_rd_poll_timeout(lan9645x, ANA_MACACCESS, val,
+				       ANA_MACACCESS_MAC_TABLE_CMD_GET(val) ==
+				       CMD_IDLE);
+	if (err)
+		return err;
+
+	if (maca)
+		*maca = val;
+
+	return 0;
+}
+
+static void lan9645x_mact_parse(u32 machi, u32 maclo, u32 maca,
+				struct lan9645x_mact_common *rentry)
+{
+	u64 addr = ANA_MACHDATA_MACHDATA_GET(machi);
+
+	addr = addr << 32 | maclo;
+	u64_to_ether_addr(addr, rentry->key.mac);
+	rentry->key.vid = ANA_MACHDATA_VID_GET(machi);
+	rentry->pgid = ANA_MACACCESS_DEST_IDX_GET(maca);
+	rentry->type = ANA_MACACCESS_ENTRYTYPE_GET(maca);
+}
+
+static void lan9645x_mac_select(struct lan9645x *lan9645x,
+				const unsigned char *addr, u16 vid)
+{
+	u64 maddr = ether_addr_to_u64(addr);
+
+	lockdep_assert_held(&lan9645x->mact_lock);
+
+	lan_wr(ANA_MACHDATA_VID_SET(vid) |
+	       ANA_MACHDATA_MACHDATA_SET(maddr >> 32),
+	       lan9645x,
+	       ANA_MACHDATA);
+
+	lan_wr(maddr & GENMASK(31, 0),
+	       lan9645x,
+	       ANA_MACLDATA);
+}
+
+static int __lan9645x_mact_forget(struct lan9645x *lan9645x,
+				  const unsigned char mac[ETH_ALEN],
+				  unsigned int vid,
+				  enum macaccess_entry_type type)
+{
+	lockdep_assert_held(&lan9645x->mact_lock);
+
+	lan9645x_mac_select(lan9645x, mac, vid);
+
+	lan_wr(ANA_MACACCESS_ENTRYTYPE_SET(type) |
+	       ANA_MACACCESS_MAC_TABLE_CMD_SET(CMD_FORGET),
+	       lan9645x,
+	       ANA_MACACCESS);
+
+	return lan9645x_mac_wait_for_completion(lan9645x, NULL);
+}
+
+int lan9645x_mact_forget(struct lan9645x *lan9645x,
+			 const unsigned char mac[ETH_ALEN], unsigned int vid,
+			 enum macaccess_entry_type type)
+{
+	int ret;
+
+	mutex_lock(&lan9645x->mact_lock);
+	ret = __lan9645x_mact_forget(lan9645x, mac, vid, type);
+	mutex_unlock(&lan9645x->mact_lock);
+
+	return ret;
+}
+
+static bool lan9645x_mac_ports_use_cpu(const unsigned char *mac,
+				       enum macaccess_entry_type type)
+{
+	u32 mc_ports;
+
+	switch (type) {
+	case ENTRYTYPE_MACV4:
+		mc_ports = (mac[1] << 8) | mac[2];
+		break;
+	case ENTRYTYPE_MACV6:
+		mc_ports = (mac[0] << 8) | mac[1];
+		break;
+	default:
+		return false;
+	}
+
+	return !!(mc_ports & BIT(CPU_PORT));
+}
+
+static int __lan9645x_mact_learn_cpu_copy(struct lan9645x *lan9645x, int port,
+					  const unsigned char *addr, u16 vid,
+					  enum macaccess_entry_type type,
+					  bool cpu_copy)
+{
+	lockdep_assert_held(&lan9645x->mact_lock);
+
+	lan9645x_mac_select(lan9645x, addr, vid);
+
+	lan_wr(ANA_MACACCESS_VALID_SET(1) |
+	       ANA_MACACCESS_DEST_IDX_SET(port) |
+	       ANA_MACACCESS_MAC_CPU_COPY_SET(cpu_copy) |
+	       ANA_MACACCESS_ENTRYTYPE_SET(type) |
+	       ANA_MACACCESS_MAC_TABLE_CMD_SET(CMD_LEARN),
+	       lan9645x, ANA_MACACCESS);
+
+	return lan9645x_mac_wait_for_completion(lan9645x, NULL);
+}
+
+static int __lan9645x_mact_learn(struct lan9645x *lan9645x, int port,
+				 const unsigned char *addr, u16 vid,
+				 enum macaccess_entry_type type)
+{
+	bool cpu_copy = lan9645x_mac_ports_use_cpu(addr, type);
+
+	return __lan9645x_mact_learn_cpu_copy(lan9645x, port, addr, vid, type,
+					      cpu_copy);
+}
+
+int lan9645x_mact_learn(struct lan9645x *lan9645x, int port,
+			const unsigned char *addr, u16 vid,
+			enum macaccess_entry_type type)
+{
+	int ret;
+
+	mutex_lock(&lan9645x->mact_lock);
+	ret = __lan9645x_mact_learn(lan9645x, port, addr, vid, type);
+	mutex_unlock(&lan9645x->mact_lock);
+
+	return ret;
+}
+
+int lan9645x_mact_flush(struct lan9645x *lan9645x, int port)
+{
+	int err = 0;
+
+	mutex_lock(&lan9645x->mact_lock);
+	/* MAC table entries with dst index matching port are aged on scan. */
+	lan_wr(ANA_ANAGEFIL_PID_EN_SET(1) |
+	       ANA_ANAGEFIL_PID_VAL_SET(port),
+	       lan9645x, ANA_ANAGEFIL);
+
+	/* Flushing requires two scans. First sets AGE_FLAG=1, second removes
+	 * entries with AGE_FLAG=1.
+	 */
+	lan_wr(ANA_MACACCESS_MAC_TABLE_CMD_SET(CMD_AGE),
+	       lan9645x,
+	       ANA_MACACCESS);
+
+	err = lan9645x_mac_wait_for_completion(lan9645x, NULL);
+	if (err)
+		goto mact_unlock;
+
+	lan_wr(ANA_MACACCESS_MAC_TABLE_CMD_SET(CMD_AGE),
+	       lan9645x,
+	       ANA_MACACCESS);
+
+	err = lan9645x_mac_wait_for_completion(lan9645x, NULL);
+
+mact_unlock:
+	lan_wr(0, lan9645x, ANA_ANAGEFIL);
+	mutex_unlock(&lan9645x->mact_lock);
+	return err;
+}
+
+int lan9645x_mact_entry_add(struct lan9645x *lan9645x, int pgid,
+			    const unsigned char *mac, u16 vid)
+{
+	struct lan9645x_mact_entry *entry;
+	int ret = 0;
+
+	/* Users can not move (vid,mac) to a different port, without removing
+	 * the original entry first. But we overwrite entry in HW, and update
+	 * software pgid for good measure.
+	 */
+	mutex_lock(&lan9645x->mac_entry_lock);
+	entry = lan9645x_mact_entry_find(lan9645x, mac, vid);
+	if (entry) {
+		entry->common.pgid = pgid;
+		mutex_unlock(&lan9645x->mac_entry_lock);
+		goto mac_learn;
+	}
+
+	entry = lan9645x_mact_entry_alloc(lan9645x, mac, vid, pgid,
+					  ENTRYTYPE_LOCKED);
+	if (!entry) {
+		mutex_unlock(&lan9645x->mac_entry_lock);
+		return -ENOMEM;
+	}
+
+	list_add_tail(&entry->list, &lan9645x->mac_entries);
+	mutex_unlock(&lan9645x->mac_entry_lock);
+
+mac_learn:
+	WARN_ON(entry->common.pgid != pgid);
+	ret = lan9645x_mact_learn(lan9645x, pgid, mac, vid, ENTRYTYPE_LOCKED);
+	if (ret) {
+		mutex_lock(&lan9645x->mac_entry_lock);
+		lan9645x_mact_entry_dealloc(lan9645x, entry);
+		mutex_unlock(&lan9645x->mac_entry_lock);
+	}
+	return ret;
+}
+
+int lan9645x_mact_entry_del(struct lan9645x *lan9645x, int pgid,
+			    const unsigned char *mac, u16 vid)
+{
+	struct lan9645x_mact_entry *entry;
+
+	mutex_lock(&lan9645x->mac_entry_lock);
+	entry = lan9645x_mact_entry_find(lan9645x, mac, vid);
+	if (entry) {
+		WARN_ON(entry->common.pgid != pgid);
+		lan9645x_mact_entry_dealloc(lan9645x, entry);
+		mutex_unlock(&lan9645x->mac_entry_lock);
+		goto forget;
+	}
+	mutex_unlock(&lan9645x->mac_entry_lock);
+	return -ENOENT;
+
+forget:
+	return lan9645x_mact_forget(lan9645x, mac, vid, ENTRYTYPE_LOCKED);
+}
+
+void lan9645x_mac_init(struct lan9645x *lan9645x)
+{
+	mutex_init(&lan9645x->mac_entry_lock);
+	mutex_init(&lan9645x->mact_lock);
+	mutex_init(&lan9645x->fwd_domain_lock);
+	INIT_LIST_HEAD(&lan9645x->mac_entries);
+
+	/* Clear the MAC table */
+	mutex_lock(&lan9645x->mact_lock);
+	lan_wr(CMD_INIT, lan9645x, ANA_MACACCESS);
+	lan9645x_mac_wait_for_completion(lan9645x, NULL);
+	mutex_unlock(&lan9645x->mact_lock);
+}
+
+void lan9645x_mac_deinit(struct lan9645x *lan9645x)
+{
+	mutex_destroy(&lan9645x->mac_entry_lock);
+	mutex_destroy(&lan9645x->mact_lock);
+	mutex_destroy(&lan9645x->fwd_domain_lock);
+}
+
+int lan9645x_mact_dsa_dump(struct lan9645x *lan9645x, int port,
+			   dsa_fdb_dump_cb_t *cb, void *data)
+{
+	struct lan9645x_mact_entry entry = { 0 };
+	u32 mach, macl, maca;
+	int err = 0;
+	u32 autoage;
+
+	mach = 0;
+	macl = 0;
+	entry.common.type = ENTRYTYPE_NORMAL;
+
+	mutex_lock(&lan9645x->mact_lock);
+
+	/* The aging filter works both for aging scans and GET_NEXT table scans.
+	 * With it, the HW table iteration only stops at entries matching our
+	 * filter. Since DSA calls us for each port on a table dump, this helps
+	 * avoid unnecessary work.
+	 *
+	 * Disable automatic aging temporarily. First save current state.
+	 */
+	autoage = lan_rd(lan9645x, ANA_AUTOAGE);
+
+	/* Disable aging */
+	lan_rmw(ANA_AUTOAGE_AGE_PERIOD_SET(0),
+		ANA_AUTOAGE_AGE_PERIOD,
+		lan9645x, ANA_AUTOAGE);
+
+	/* Setup filter on our port */
+	lan_wr(ANA_ANAGEFIL_PID_EN_SET(1) |
+	       ANA_ANAGEFIL_PID_VAL_SET(port),
+	       lan9645x, ANA_ANAGEFIL);
+
+	lan_wr(0, lan9645x, ANA_MACHDATA);
+	lan_wr(0, lan9645x, ANA_MACLDATA);
+
+	while (1) {
+		/* NOTE: we rely on mach, macl and type being set correctly in
+		 * the registers from previous round, vis a vis the GET_NEXT
+		 * semantics, so locking entire loop is important.
+		 */
+		lan_wr(ANA_MACACCESS_MAC_TABLE_CMD_SET(CMD_GET_NEXT) |
+		       ANA_MACACCESS_ENTRYTYPE_SET(entry.common.type),
+		       lan9645x, ANA_MACACCESS);
+
+		if (lan9645x_mac_wait_for_completion(lan9645x, &maca))
+			break;
+
+		if (ANA_MACACCESS_VALID_GET(maca) == 0)
+			break;
+
+		mach = lan_rd(lan9645x, ANA_MACHDATA);
+		macl = lan_rd(lan9645x, ANA_MACLDATA);
+
+		lan9645x_mact_parse(mach, macl, maca, &entry.common);
+
+		if (ANA_MACACCESS_DEST_IDX_GET(maca) == port &&
+		    entry.common.type == ENTRYTYPE_NORMAL) {
+			if (entry.common.key.vid > VLAN_MAX)
+				entry.common.key.vid = 0;
+
+			err = cb(entry.common.key.mac, entry.common.key.vid,
+				 false, data);
+			if (err)
+				break;
+		}
+	}
+
+	/* Remove aging filters and restore aging */
+	lan_wr(0, lan9645x, ANA_ANAGEFIL);
+	lan_rmw(ANA_AUTOAGE_AGE_PERIOD_SET(ANA_AUTOAGE_AGE_PERIOD_GET(autoage)),
+		ANA_AUTOAGE_AGE_PERIOD,
+		lan9645x, ANA_AUTOAGE);
+
+	mutex_unlock(&lan9645x->mact_lock);
+
+	return err;
+}
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
index 1c8f20452487..ba76279b4414 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.c
@@ -78,6 +78,7 @@ static void lan9645x_teardown(struct dsa_switch *ds)
 
 	debugfs_remove_recursive(lan9645x->debugfs_root);
 	lan9645x_npi_port_deinit(lan9645x, lan9645x->npi);
+	lan9645x_mac_deinit(lan9645x);
 }
 
 static int lan9645x_change_mtu(struct dsa_switch *ds, int port, int new_mtu)
@@ -171,8 +172,8 @@ static int lan9645x_setup(struct dsa_switch *ds)
 		return err;
 	}
 
-	mutex_init(&lan9645x->fwd_domain_lock);
 	lan9645x_vlan_init(lan9645x);
+	lan9645x_mac_init(lan9645x);
 
 	/* Link Aggregation Mode: NETDEV_LAG_HASH_L2 */
 	lan_wr(ANA_AGGR_CFG_AC_SMAC_ENA |
@@ -534,6 +535,107 @@ static int lan9645x_port_vlan_del(struct dsa_switch *ds, int port,
 	return 0;
 }
 
+static void lan9645x_port_fast_age(struct dsa_switch *ds, int port)
+{
+	lan9645x_mact_flush(ds->priv, port);
+}
+
+static int lan9645x_fdb_dump(struct dsa_switch *ds, int port,
+			     dsa_fdb_dump_cb_t *cb, void *data)
+{
+	return lan9645x_mact_dsa_dump(ds->priv, port, cb, data);
+}
+
+static int __lan9645x_fdb_add(struct lan9645x *lan9645x, int pgid,
+			      const unsigned char *mac, u16 vid,
+			      struct net_device *bridge)
+{
+	if (!vid)
+		vid = lan9645x_vlan_unaware_pvid(!!bridge);
+
+	return lan9645x_mact_entry_add(lan9645x, pgid, mac, vid);
+}
+
+static struct net_device *lan9645x_db2bridge(struct dsa_db db)
+{
+	switch (db.type) {
+	case DSA_DB_PORT:
+	case DSA_DB_LAG:
+		return NULL;
+	case DSA_DB_BRIDGE:
+		return db.bridge.dev;
+	default:
+		return ERR_PTR(-EOPNOTSUPP);
+	}
+}
+
+static int lan9645x_fdb_add(struct dsa_switch *ds, int port,
+			    const unsigned char *addr, u16 vid,
+			    struct dsa_db db)
+{
+	struct net_device *br = lan9645x_db2bridge(db);
+	struct dsa_port *dp = dsa_to_port(ds, port);
+	struct lan9645x *lan9645x = ds->priv;
+
+	if (IS_ERR(br))
+		return PTR_ERR(br);
+
+	if (dsa_port_is_cpu(dp) && !br &&
+	    dsa_fdb_present_in_other_db(ds, port, addr, vid, db))
+		return 0;
+
+	if (dsa_port_is_cpu(dp)) {
+		/* Trap DSA cpu port */
+		return lan9645x_mact_learn(lan9645x, PGID_CPU, addr,
+					   lan9645x_vlan_unaware_pvid(!!br),
+					   ENTRYTYPE_LOCKED);
+	}
+
+	return __lan9645x_fdb_add(lan9645x, port, addr, vid, br);
+}
+
+static int __lan9645x_fdb_del(struct lan9645x *lan9645x, int port,
+			      const unsigned char *addr, u16 vid,
+			      struct net_device *bridge)
+{
+	int err;
+
+	if (!vid)
+		vid = lan9645x_vlan_unaware_pvid(!!bridge);
+
+	err = lan9645x_mact_entry_del(lan9645x, port, addr, vid);
+	if (err == -ENOENT) {
+		dev_dbg(lan9645x->dev, "fdb not found mac %pM vid %u pgid %u",
+			addr, vid, port);
+		return 0;
+	}
+	return err;
+}
+
+static int lan9645x_fdb_del(struct dsa_switch *ds, int port,
+			    const unsigned char *addr, u16 vid,
+			    struct dsa_db db)
+{
+	struct net_device *br = lan9645x_db2bridge(db);
+	struct dsa_port *dp = dsa_to_port(ds, port);
+	struct lan9645x *lan9645x = ds->priv;
+
+	if (IS_ERR(br))
+		return PTR_ERR(br);
+
+	if (dsa_port_is_cpu(dp) && !br &&
+	    dsa_fdb_present_in_other_db(ds, port, addr, vid, db))
+		return 0;
+
+	if (dsa_port_is_cpu(dp)) {
+		return lan9645x_mact_forget(lan9645x, addr,
+					    lan9645x_vlan_unaware_pvid(!!br),
+					    ENTRYTYPE_LOCKED);
+	}
+
+	return __lan9645x_fdb_del(lan9645x, port, addr, vid, br);
+}
+
 static const struct dsa_switch_ops lan9645x_switch_ops = {
 	.get_tag_protocol		= lan9645x_get_tag_protocol,
 	.connect_tag_protocol		= lan9645x_connect_tag_protocol,
@@ -560,6 +662,12 @@ static const struct dsa_switch_ops lan9645x_switch_ops = {
 	.port_vlan_filtering		= lan9645x_port_vlan_filtering,
 	.port_vlan_add			= lan9645x_port_vlan_add,
 	.port_vlan_del			= lan9645x_port_vlan_del,
+
+	/* MAC table integration */
+	.port_fast_age			= lan9645x_port_fast_age,
+	.port_fdb_dump			= lan9645x_fdb_dump,
+	.port_fdb_add			= lan9645x_fdb_add,
+	.port_fdb_del			= lan9645x_fdb_del,
 };
 
 static int lan9645x_request_target_regmaps(struct lan9645x *lan9645x)
diff --git a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
index f1471344a9e9..4c7111375918 100644
--- a/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
+++ b/drivers/net/dsa/microchip/lan9645x/lan9645x_main.h
@@ -158,6 +158,34 @@ enum lan9645x_vlan_port_tag {
 	LAN9645X_TAG_ALL = 3,
 };
 
+/* MAC table entry types.
+ * ENTRYTYPE_NORMAL is subject to aging.
+ * ENTRYTYPE_LOCKED is not subject to aging.
+ * ENTRYTYPE_MACv4 is not subject to aging. For IPv4 multicast.
+ * ENTRYTYPE_MACv6 is not subject to aging. For IPv6 multicast.
+ */
+enum macaccess_entry_type {
+	ENTRYTYPE_NORMAL = 0,
+	ENTRYTYPE_LOCKED,
+	ENTRYTYPE_MACV4,
+	ENTRYTYPE_MACV6,
+};
+
+struct lan9645x_mact_common {
+	struct lan9645x_mact_key {
+		u16 vid;
+		u8 mac[ETH_ALEN] __aligned(2);
+	} key;
+	u32 row: 11, /* 2048 rows, 4 buckets each */
+	    pgid: 6, /* 0-63 general purpose pgids. */
+	    type: 2;
+};
+
+struct lan9645x_mact_entry {
+	struct lan9645x_mact_common common;
+	struct list_head list;
+};
+
 struct lan9645x {
 	struct device *dev;
 	struct dsa_switch *ds;
@@ -180,6 +208,9 @@ struct lan9645x {
 	u16 bridge_mask; /* Mask for bridged ports */
 	u16 bridge_fwd_mask; /* Mask for forwarding bridged ports */
 	struct mutex fwd_domain_lock; /* lock forwarding configuration */
+	struct list_head mac_entries;
+	struct mutex mact_lock; /* lock access to mact_table */
+	struct mutex mac_entry_lock; /* lock for mac_entries list */
 
 	/* VLAN */
 	u16 vlan_mask[VLAN_N_VID]; /* Port mask per vlan */
@@ -424,4 +455,21 @@ void lan9645x_vlan_set_hostmode(struct lan9645x_port *p);
 int lan9645x_port_vlan_prepare(struct lan9645x_port *p, u16 vid, bool pvid,
 			       bool untagged, struct netlink_ext_ack *extack);
 
+/* MAC table: lan9645x_mac.c */
+int lan9645x_mact_flush(struct lan9645x *lan9645x, int port);
+int lan9645x_mact_learn(struct lan9645x *lan9645x, int port,
+			const unsigned char *addr, u16 vid,
+			enum macaccess_entry_type type);
+int lan9645x_mact_forget(struct lan9645x *lan9645x,
+			 const unsigned char mac[ETH_ALEN], unsigned int vid,
+			 enum macaccess_entry_type type);
+void lan9645x_mac_init(struct lan9645x *lan9645x);
+void lan9645x_mac_deinit(struct lan9645x *lan9645x);
+int lan9645x_mact_dsa_dump(struct lan9645x *lan9645x, int port,
+			   dsa_fdb_dump_cb_t *cb, void *data);
+int lan9645x_mact_entry_del(struct lan9645x *lan9645x, int pgid,
+			    const unsigned char *mac, u16 vid);
+int lan9645x_mact_entry_add(struct lan9645x *lan9645x, int pgid,
+			    const unsigned char *mac, u16 vid);
+
 #endif /* __LAN9645X_MAIN_H__ */

-- 
2.52.0