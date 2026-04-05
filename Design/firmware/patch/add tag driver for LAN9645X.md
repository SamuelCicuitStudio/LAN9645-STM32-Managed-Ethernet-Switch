add tag driver for LAN9645X.md
Add tag driver for LAN9645x using a front port as CPU port. This mode
is called an NPI port in the datasheet.
Use long prefix on extraction (RX) and no prefix on injection (TX). A
long prefix on extraction helps get through the conduit port on host
side, since it will see a broadcast MAC.

The LAN9645x chip is in the same design architecture family as ocelot
and lan966x. The tagging protocol has the same structure as these chips,
but the particular fields are different or have different sizes.
Therefore, this tag driver is similar to tag_ocelot.c, but the
differences in fields makes it hard to reuse.

LAN9645x supports 3 different tag formats for extraction/injection of
frames from a CPU port: long prefix, short prefix and no prefix.

The tag is prepended to the frame. The critical data for the chip is
contained in an internal frame header (IFH) which is 28 bytes. The
prefix formats look like this:

Long prefix (16 bytes) + IFH:
- DMAC    = 0xffffffffffff on extraction.
- SMAC    = 0xfeffffffffff on extraction.
- ETYPE   = 0x8880
- payload = 0x0011
- IFH

Short prefix (4 bytes) + IFH:
- 0x8880
- 0x0011
- IFH

No prefix:
- IFH

The format can be configured asymmetrically on RX and TX.

Reviewed-by: Steen Hegelund <Steen.Hegelund@xxxxxxxxxxxxx>
Signed-off-by: Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
---
 MAINTAINERS                  |   8 ++
 include/linux/dsa/lan9645x.h | 290 +++++++++++++++++++++++++++++++++++++++++++
 include/net/dsa.h            |   2 +
 net/dsa/Kconfig              |  10 ++
 net/dsa/Makefile             |   1 +
 net/dsa/tag_lan9645x.c       | 143 +++++++++++++++++++++
 6 files changed, 454 insertions(+)

diff --git a/MAINTAINERS b/MAINTAINERS
index 2265e2c9bfbe..2712aaf7cedd 100644
--- a/MAINTAINERS
+++ b/MAINTAINERS
@@ -17286,6 +17286,14 @@ L:	netdev@xxxxxxxxxxxxxxx
 S:	Maintained
 F:	drivers/net/phy/microchip_t1.c
 
+MICROCHIP LAN9645X ETHERNET SWITCH DRIVER
+M:	Jens Emil Schulz Østergaard <jensemil.schulzostergaard@xxxxxxxxxxxxx>
+M:	UNGLinuxDriver@xxxxxxxxxxxxx
+L:	netdev@xxxxxxxxxxxxxxx
+S:	Maintained
+F:	include/linux/dsa/lan9645x.h
+F:	net/dsa/tag_lan9645x.c
+
 MICROCHIP LAN966X ETHERNET DRIVER
 M:	Horatiu Vultur <horatiu.vultur@xxxxxxxxxxxxx>
 M:	UNGLinuxDriver@xxxxxxxxxxxxx
diff --git a/include/linux/dsa/lan9645x.h b/include/linux/dsa/lan9645x.h
new file mode 100644
index 000000000000..37b74dde9611
--- /dev/null
+++ b/include/linux/dsa/lan9645x.h
@@ -0,0 +1,290 @@
+/* SPDX-License-Identifier: GPL-2.0
+ * Copyright (C) 2026 Microchip Technology Inc.
+ */
+
+#ifndef _NET_DSA_TAG_LAN9645X_H_
+#define _NET_DSA_TAG_LAN9645X_H_
+
+#include <linux/if_bridge.h>
+#include <linux/if_vlan.h>
+#include <net/dsa.h>
+
+/* LAN9645x supports 3 different formats on an NPI port, long prefix, short
+ * prefix and no prefix. The format can be configured asymmetrically on RX and
+ * TX. We use long prefix on extraction (RX), and no prefix on injection.
+ * The long prefix on extraction helps get through the conduit port on host
+ * side, since it will see a broadcast MAC.
+ *
+ * The internal frame header (IFH) is 28 bytes, and the fields are documented
+ * below.
+ *
+ * Long prefix, 16 bytes + IFH:
+ * - DMAC    = 0xFFFFFFFFFFFF on extraction.
+ * - SMAC    = 0xFEFFFFFFFFFF on extraction.
+ * - ETYPE   = 0x8880
+ * - payload = 0x0011
+ * - IFH
+ *
+ * Short prefix, 4 bytes + IFH:
+ * - 0x8880
+ * - 0x0011
+ * - IFH
+ *
+ * No prefix:
+ * - IFH
+ *
+ */
+#define LAN9645X_IFH_TAG_TYPE_C	0
+#define LAN9645X_IFH_TAG_TYPE_S	1
+#define LAN9645X_IFH_LEN_U32		7
+#define LAN9645X_IFH_LEN		(LAN9645X_IFH_LEN_U32 * sizeof(u32))
+#define LAN9645X_IFH_BITS		(LAN9645X_IFH_LEN * BITS_PER_BYTE)
+#define LAN9645X_SHORT_PREFIX_LEN	4
+#define LAN9645X_LONG_PREFIX_LEN	16
+#define LAN9645X_TOTAL_TAG_LEN (LAN9645X_LONG_PREFIX_LEN + LAN9645X_IFH_LEN)
+
+#define IFH_INJ_TIMESTAMP		192
+#define IFH_BYPASS			191
+#define IFH_MASQ			190
+#define IFH_TIMESTAMP			186
+#define IFH_TIMESTAMP_NS		194
+#define IFH_TIMESTAMP_SUBNS		186
+#define IFH_MASQ_PORT			186
+#define IFH_RCT_INJ			185
+#define IFH_LEN				171
+#define IFH_WRDMODE			169
+#define IFH_RTAGD			167
+#define IFH_CUTTHRU			166
+#define IFH_REW_CMD			156
+#define IFH_REW_OAM			155
+#define IFH_PDU_TYPE			151
+#define IFH_FCS_UPD			150
+#define IFH_DP				149
+#define IFH_RTE_INB_UPDATE		148
+#define IFH_POP_CNT			146
+#define IFH_ETYPE_OFS			144
+#define IFH_SRCPORT			140
+#define IFH_SEQ_NUM			120
+#define IFH_TAG_TYPE			119
+#define IFH_TCI				103
+#define IFH_DSCP			97
+#define IFH_QOS_CLASS			94
+#define IFH_CPUQ			86
+#define IFH_LEARN_FLAGS			84
+#define IFH_SFLOW_ID			80
+#define IFH_ACL_HIT			79
+#define IFH_ACL_IDX			73
+#define IFH_ISDX			65
+#define IFH_DSTS			55
+#define IFH_FLOOD			53
+#define IFH_SEQ_OP			51
+#define IFH_IPV				48
+#define IFH_AFI				47
+#define IFH_RTP_ID			37
+#define IFH_RTP_SUBID			36
+#define IFH_PN_DATA_STATUS		28
+#define IFH_PN_TRANSF_STATUS_ZERO	27
+#define IFH_PN_CC			11
+#define IFH_DUPL_DISC_ENA		10
+#define IFH_RCT_AVAIL			9
+
+#define IFH_INJ_TIMESTAMP_SZ		32
+#define IFH_BYPASS_SZ			1
+#define IFH_MASQ_SZ			1
+#define IFH_TIMESTAMP_SZ		38
+#define IFH_TIMESTAMP_NS_SZ		30
+#define IFH_TIMESTAMP_SUBNS_SZ		8
+#define IFH_MASQ_PORT_SZ		4
+#define IFH_RCT_INJ_SZ			1
+#define IFH_LEN_SZ			14
+#define IFH_WRDMODE_SZ			2
+#define IFH_RTAGD_SZ			2
+#define IFH_CUTTHRU_SZ			1
+#define IFH_REW_CMD_SZ			10
+#define IFH_REW_OAM_SZ			1
+#define IFH_PDU_TYPE_SZ			4
+#define IFH_FCS_UPD_SZ			1
+#define IFH_DP_SZ			1
+#define IFH_RTE_INB_UPDATE_SZ		1
+#define IFH_POP_CNT_SZ			2
+#define IFH_ETYPE_OFS_SZ		2
+#define IFH_SRCPORT_SZ			4
+#define IFH_SEQ_NUM_SZ			16
+#define IFH_TAG_TYPE_SZ			1
+#define IFH_TCI_SZ			16
+#define IFH_DSCP_SZ			6
+#define IFH_QOS_CLASS_SZ		3
+#define IFH_CPUQ_SZ			8
+#define IFH_LEARN_FLAGS_SZ		2
+#define IFH_SFLOW_ID_SZ			4
+#define IFH_ACL_HIT_SZ			1
+#define IFH_ACL_IDX_SZ			6
+#define IFH_ISDX_SZ			8
+#define IFH_DSTS_SZ			10
+#define IFH_FLOOD_SZ			2
+#define IFH_SEQ_OP_SZ			2
+#define IFH_IPV_SZ			3
+#define IFH_AFI_SZ			1
+#define IFH_RTP_ID_SZ			10
+#define IFH_RTP_SUBID_SZ		1
+#define IFH_PN_DATA_STATUS_SZ		8
+#define IFH_PN_TRANSF_STATUS_ZERO_SZ	1
+#define IFH_PN_CC_SZ			16
+#define IFH_DUPL_DISC_ENA_SZ		1
+#define IFH_RCT_AVAIL_SZ		1
+
+#define LAN9645X_VALIDATE_FIELD(_fld, _fld_sz)				\
+do {									\
+	BUILD_BUG_ON_MSG((_fld_sz) > 32, "IFH field size wider than 32.");\
+	BUILD_BUG_ON_MSG((_fld_sz) == 0, "IFH field size of 0.");	\
+	BUILD_BUG_ON_MSG((_fld) + (_fld_sz) > LAN9645X_IFH_BITS,	\
+			 "IFH field overflows IFH");			\
+} while (0)
+
+#define LAN9645X_IFH_GET(_ifh, _fld) \
+({ \
+	LAN9645X_VALIDATE_FIELD(_fld, _fld##_SZ);\
+	lan9645x_ifh_get((_ifh), (_fld), _fld##_SZ); \
+})
+
+#define LAN9645X_IFH_SET(_ifh, _fld, _val) \
+({ \
+	LAN9645X_VALIDATE_FIELD(_fld, _fld##_SZ);\
+	lan9645x_ifh_set((_ifh), (_val), (_fld), _fld##_SZ); \
+})
+
+#define BTM_MSK(n)	((u8)GENMASK(n, 0))
+#define TOP_MSK(n)	((u8)GENMASK(7, n))
+
+static inline void set_merge_mask(u8 *on_zero, u8 on_one, u8 mask)
+{
+	*on_zero =  *on_zero ^ ((*on_zero ^ on_one) & mask);
+}
+
+/* The internal frame header (IFH) is a big-endian 28 byte unpadded bit array.
+ * Frames can be prepended with an IFH on injection and extraction. There
+ * are two field layouts, one for extraction and one for injection.
+ *
+ *    IFH bits go from high to low, for instance
+ *    ifh[0]  = [223:215]
+ *    ifh[27] = [7:0]
+ *
+ * Here is an example of setting a value starting at bit 13 of bit length 17.
+ *
+ * val    = 0x1ff
+ * pos    = 13
+ * length = 17
+ *
+ *
+ * IFH[]   0                         23       24       25        26      27
+ *
+ *                                           end_u8           start_u8
+ *      +--------+----------------+--------+--------+--------+--------+--------+
+ *      |        |                |        |        |        |        |        |
+ * IFH  |        | ....           |        |  vvvvvvvvvvvvvvvvvvv     |        |
+ *      |        |                |        |  |     |        |  |     |        |
+ *      +--------+----------------+--------+--+-----+--------+--+-----+--------+
+ * Bits  223                       39    32 31|   24 23    16 15|    8 7      0
+ *                                            |                 |
+ *                                            |                 |
+ *                                            |                 |
+ *                                            v                 v
+ *                                        end       = 29       pos        = 13
+ *                                        end_rem   = 5        pos_rem    = 5
+ *                                        end_u8    = 3        start_u8   = 1
+ *                                        BTM_MSK(5)= 0x3f     TOP_MSK(5) = 0xe0
+ *
+ *
+ * In end_u8 and start_u8 we must merge the existing IFH byte with the new
+ * value. In the 'middle' bytes of the value we can overwrite the corresponding
+ * IFH byte.
+ */
+static inline void lan9645x_ifh_set(u8 *ifh, u32 val, size_t pos, size_t length)
+{
+	size_t end = (pos + length) - 1;
+	size_t start_u8 = pos >> 3;
+	size_t end_u8 = end >> 3;
+	size_t end_rem = end & 0x7;
+	size_t pos_rem = pos & 0x7;
+	u8 end_mask, start_mask;
+	size_t vshift;
+	u8 *ptr;
+
+	end_mask = BTM_MSK(end_rem);
+	start_mask = TOP_MSK(pos_rem);
+
+	ptr = &ifh[LAN9645X_IFH_LEN - 1 - end_u8];
+
+	if (end_u8 == start_u8)
+		return set_merge_mask(ptr, val << pos_rem,
+				      end_mask & start_mask);
+
+	vshift = length - end_rem - 1;
+	set_merge_mask(ptr++, val >> vshift, end_mask);
+
+	for (size_t j = 1; j < end_u8 - start_u8; j++) {
+		vshift -= 8;
+		*ptr++ = val >> vshift;
+	}
+
+	set_merge_mask(ptr, val << pos_rem, start_mask);
+}
+
+static inline u32 lan9645x_ifh_get(const u8 *ifh, size_t pos, size_t length)
+{
+	size_t end = (pos + length) - 1;
+	size_t start_u8 = pos >> 3;
+	size_t end_u8 = end >> 3;
+	size_t end_rem = end & 0x7;
+	size_t pos_rem = pos & 0x7;
+	u8 end_mask, start_mask;
+	const u8 *ptr;
+	u32 val;
+
+	end_mask = BTM_MSK(end_rem);
+	start_mask = TOP_MSK(pos_rem);
+
+	ptr = &ifh[LAN9645X_IFH_LEN - 1 - end_u8];
+
+	if (end_u8 == start_u8)
+		return (*ptr & end_mask & start_mask) >> pos_rem;
+
+	val = *ptr++ & end_mask;
+
+	for (size_t j = 1; j < end_u8 - start_u8; j++)
+		val = val << 8 | *ptr++;
+
+	return val << (8 - pos_rem) | (*ptr & start_mask) >> pos_rem;
+}
+
+static inline void lan9645x_xmit_get_vlan_info(struct sk_buff *skb,
+					       struct net_device *br,
+					       u32 *vlan_tci, u32 *tag_type)
+{
+	struct vlan_ethhdr *hdr;
+	u16 proto, tci;
+
+	if (!br || !br_vlan_enabled(br)) {
+		*vlan_tci = 0;
+		*tag_type = LAN9645X_IFH_TAG_TYPE_C;
+		return;
+	}
+
+	hdr = (struct vlan_ethhdr *)skb_mac_header(skb);
+	br_vlan_get_proto(br, &proto);
+
+	if (ntohs(hdr->h_vlan_proto) == proto) {
+		vlan_remove_tag(skb, &tci);
+		*vlan_tci = tci;
+	} else {
+		rcu_read_lock();
+		br_vlan_get_pvid_rcu(br, &tci);
+		rcu_read_unlock();
+		*vlan_tci = tci;
+	}
+
+	*tag_type = (proto != ETH_P_8021Q) ? LAN9645X_IFH_TAG_TYPE_S :
+					     LAN9645X_IFH_TAG_TYPE_C;
+}
+
+#endif /* _NET_DSA_TAG_LAN9645X_H_ */
diff --git a/include/net/dsa.h b/include/net/dsa.h
index 6c17446f3dcc..977b35aa9f16 100644
--- a/include/net/dsa.h
+++ b/include/net/dsa.h
@@ -58,6 +58,7 @@ struct tc_action;
 #define DSA_TAG_PROTO_YT921X_VALUE		30
 #define DSA_TAG_PROTO_MXL_GSW1XX_VALUE		31
 #define DSA_TAG_PROTO_MXL862_VALUE		32
+#define DSA_TAG_PROTO_LAN9645X_VALUE		33
 
 enum dsa_tag_protocol {
 	DSA_TAG_PROTO_NONE		= DSA_TAG_PROTO_NONE_VALUE,
@@ -93,6 +94,7 @@ enum dsa_tag_protocol {
 	DSA_TAG_PROTO_YT921X		= DSA_TAG_PROTO_YT921X_VALUE,
 	DSA_TAG_PROTO_MXL_GSW1XX	= DSA_TAG_PROTO_MXL_GSW1XX_VALUE,
 	DSA_TAG_PROTO_MXL862		= DSA_TAG_PROTO_MXL862_VALUE,
+	DSA_TAG_PROTO_LAN9645X		= DSA_TAG_PROTO_LAN9645X_VALUE,
 };
 
 struct dsa_switch;
diff --git a/net/dsa/Kconfig b/net/dsa/Kconfig
index 5ed8c704636d..8592cccde7ff 100644
--- a/net/dsa/Kconfig
+++ b/net/dsa/Kconfig
@@ -211,4 +211,14 @@ config NET_DSA_TAG_YT921X
 	  Say Y or M if you want to enable support for tagging frames for
 	  Motorcomm YT921x switches.
 
+config NET_DSA_TAG_LAN9645X
+	tristate "Tag driver for Lan9645x switches"
+	help
+	  Say Y or M if you want to enable NPI tagging for the Lan9645x switches.
+	  In this mode, the frames over the Ethernet CPU port are prepended with
+	  a hardware-defined injection/extraction frame header.
+	  On injection a 28 byte internal frame header (IFH) is used. On
+	  extraction a 16 byte prefix is prepended before the internal frame
+	  header. This prefix starts with a broadcast MAC, to ease passage
+	  through the host side RX filter.
 endif
diff --git a/net/dsa/Makefile b/net/dsa/Makefile
index bf7247759a64..dddcd85c81ce 100644
--- a/net/dsa/Makefile
+++ b/net/dsa/Makefile
@@ -42,6 +42,7 @@ obj-$(CONFIG_NET_DSA_TAG_TRAILER) += tag_trailer.o
 obj-$(CONFIG_NET_DSA_TAG_VSC73XX_8021Q) += tag_vsc73xx_8021q.o
 obj-$(CONFIG_NET_DSA_TAG_XRS700X) += tag_xrs700x.o
 obj-$(CONFIG_NET_DSA_TAG_YT921X) += tag_yt921x.o
+obj-$(CONFIG_NET_DSA_TAG_LAN9645X) += tag_lan9645x.o
 
 # for tracing framework to find trace.h
 CFLAGS_trace.o := -I$(src)
diff --git a/net/dsa/tag_lan9645x.c b/net/dsa/tag_lan9645x.c
new file mode 100644
index 000000000000..39eb2fb388e5
--- /dev/null
+++ b/net/dsa/tag_lan9645x.c
@@ -0,0 +1,143 @@
+// SPDX-License-Identifier: GPL-2.0
+/* Copyright (C) 2026 Microchip Technology Inc.
+ */
+
+#include <linux/dsa/lan9645x.h>
+#include <linux/if_ether.h>
+#include <linux/if_hsr.h>
+#include <linux/if_vlan.h>
+#include <linux/igmp.h>
+#include <linux/kernel.h>
+#include <linux/netdevice.h>
+#include <linux/skbuff.h>
+#include <net/addrconf.h>
+#include <net/dsa.h>
+
+#include "tag.h"
+
+#define LAN9645X_NAME "lan9645x"
+
+static struct sk_buff *lan9645x_xmit(struct sk_buff *skb,
+				     struct net_device *ndev)
+{
+	struct dsa_port *dp = dsa_user_to_port(ndev);
+	struct dsa_switch *ds = dp->ds;
+	u32 cpu_port = ds->num_ports;
+	u32 vlan_tci, tag_type;
+	u32 qos_class;
+	void *ifh;
+
+	lan9645x_xmit_get_vlan_info(skb, dsa_port_bridge_dev_get(dp), &vlan_tci,
+				    &tag_type);
+
+	qos_class = netdev_get_num_tc(ndev) ?
+		netdev_get_prio_tc_map(ndev, skb->priority) :
+		skb->priority;
+
+	/* Make room for IFH */
+	ifh = skb_push(skb, LAN9645X_IFH_LEN);
+	memset(ifh, 0, LAN9645X_IFH_LEN);
+
+	LAN9645X_IFH_SET(ifh, IFH_BYPASS, 1);
+	LAN9645X_IFH_SET(ifh, IFH_SRCPORT, cpu_port);
+	LAN9645X_IFH_SET(ifh, IFH_QOS_CLASS, qos_class);
+	LAN9645X_IFH_SET(ifh, IFH_TCI, vlan_tci);
+	LAN9645X_IFH_SET(ifh, IFH_TAG_TYPE, tag_type);
+	LAN9645X_IFH_SET(ifh, IFH_DSTS, BIT(dp->index));
+
+	return skb;
+}
+
+static struct sk_buff *lan9645x_rcv(struct sk_buff *skb,
+				    struct net_device *ndev)
+{
+	u32 src_port, qos_class, vlan_tci, tag_type, popcnt, etype_ofs;
+	u8 *orig_skb_data = skb->data;
+	struct dsa_port *dp;
+	u32 ifh_gap_len = 0;
+	u16 vlan_tpid;
+	u8 *ifh;
+
+	/* DSA master already consumed DMAC,SMAC,ETYPE from long prefix. Go back
+	 * to beginning of frame.
+	 */
+	skb_push(skb, ETH_HLEN);
+	/* IFH starts after our long prefix */
+	ifh = skb_pull(skb, LAN9645X_LONG_PREFIX_LEN);
+
+	src_port = LAN9645X_IFH_GET(ifh, IFH_SRCPORT);
+	qos_class = LAN9645X_IFH_GET(ifh, IFH_QOS_CLASS);
+	tag_type = LAN9645X_IFH_GET(ifh, IFH_TAG_TYPE);
+	vlan_tci = LAN9645X_IFH_GET(ifh, IFH_TCI);
+	popcnt = LAN9645X_IFH_GET(ifh, IFH_POP_CNT);
+	etype_ofs = LAN9645X_IFH_GET(ifh, IFH_ETYPE_OFS);
+
+	/* Set skb->data at start of real header
+	 *
+	 * Since REW_PORT_NO_REWRITE=0 is required on the NPI port, we need to
+	 * account for any tags popped by the hardware, as that will leave a gap
+	 * between the IFH and DMAC.
+	 */
+	if (popcnt == 0 && etype_ofs == 0)
+		ifh_gap_len = 2 * VLAN_HLEN;
+	else if (popcnt == 3)
+		ifh_gap_len = VLAN_HLEN;
+
+	skb_pull(skb, LAN9645X_IFH_LEN + ifh_gap_len);
+	skb_reset_mac_header(skb);
+	skb_set_network_header(skb, ETH_HLEN);
+	skb_reset_mac_len(skb);
+
+	/* Reset skb->data past the actual ethernet header. */
+	skb_pull(skb, ETH_HLEN);
+	skb_postpull_rcsum(skb, orig_skb_data,
+			   LAN9645X_TOTAL_TAG_LEN + ifh_gap_len);
+
+	skb->dev = dsa_conduit_find_user(ndev, 0, src_port);
+	if (WARN_ON_ONCE(!skb->dev)) {
+		/* This should never happen since we have disabled reflection
+		 * back to CPU_PORT.
+		 */
+		return NULL;
+	}
+
+	dsa_default_offload_fwd_mark(skb);
+
+	skb->priority = qos_class;
+
+	/* While we have REW_PORT_NO_REWRITE=0 on the NPI port, we still disable
+	 * port VLAN tagging with REW_TAG_CFG. Any classified VID, different
+	 * from a VID in the frame, will not be written to the frame, but is
+	 * only communicated via the IFH. So for VLAN-aware ports we add the IFH
+	 * vlan to the skb.
+	 */
+	dp = dsa_user_to_port(skb->dev);
+	vlan_tpid = tag_type ? ETH_P_8021AD : ETH_P_8021Q;
+
+	if (dsa_port_is_vlan_filtering(dp) &&
+	    eth_hdr(skb)->h_proto == htons(vlan_tpid)) {
+		u16 dummy_vlan_tci;
+
+		skb_push_rcsum(skb, ETH_HLEN);
+		__skb_vlan_pop(skb, &dummy_vlan_tci);
+		skb_pull_rcsum(skb, ETH_HLEN);
+		__vlan_hwaccel_put_tag(skb, htons(vlan_tpid), vlan_tci);
+	}
+
+	return skb;
+}
+
+static const struct dsa_device_ops lan9645x_netdev_ops = {
+	.name = LAN9645X_NAME,
+	.proto = DSA_TAG_PROTO_LAN9645X,
+	.xmit = lan9645x_xmit,
+	.rcv = lan9645x_rcv,
+	.needed_headroom = LAN9645X_TOTAL_TAG_LEN,
+	.promisc_on_conduit = false,
+};
+
+MODULE_DESCRIPTION("DSA tag driver for LAN9645x family of switches, using NPI port");
+MODULE_LICENSE("GPL");
+MODULE_ALIAS_DSA_TAG_DRIVER(DSA_TAG_PROTO_LAN9645X, LAN9645X_NAME);
+
+module_dsa_tag_driver(lan9645x_netdev_ops);

-- 
2.52.0


