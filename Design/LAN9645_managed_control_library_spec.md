# LAN9645 Managed Control Library Specification (STM32)

## Purpose
This document is the base specification for a reusable STM32 library that gives structured control of LAN9645 in managed mode:
- reliable SPI read/write access
- clear helper APIs by feature domain
- full control flow for enable/disable/config/status operations
- test-oriented bring-up for CuPHY and SGMII/SFP ports

It is aligned to your current hardware profile:
- 128-TQFP
- managed mode + SPI client (`MGMT_MODE[1:0]=11`)
- RMII1 to STM32
- CuPHY0 + CuPHY1 active
- SerDes0 + SerDes1 active
- external 125 MHz on `S0_CLKP`
- `SD_125M` and `SGPIO_EN` selected by external jumper/strap hardware

---

## 1) Library Architecture

### 1.1 Layers
1. Transport layer (`lan9645_spi.c`)
- SPI transaction framing for SI client protocol
- retry/timeout handling
- bus lock API (thread-safe usage)

2. Register access layer (`lan9645_reg.c`)
- `read32`, `write32`, `rmw32`, `set_bits`, `clear_bits`
- address conversion helper: `SI_ADDR = (REG_ADDR & 0x01FFFFFF) >> 2`
- optional read padding setup (`DEVCPU_GCB::IF_CFGSTAT`)

3. Feature domain layer (`lan9645_port.c`, `lan9645_phy.c`, `lan9645_vlan.c`, ...)
- typed helpers for each functional area
- translates user intent into register operations

4. Policy/profile layer (`lan9645_profile_managed_rmii1.c`)
- your board-specific defaults
- applies full startup sequence for your exact straps and wiring

### 1.2 File layout (recommended)
- `inc/lan9645/lan9645.h`
- `inc/lan9645/lan9645_regs.h` (symbolic register map)
- `src/lan9645_spi.c`
- `src/lan9645_reg.c`
- `src/lan9645_core.c`
- `src/lan9645_port.c`
- `src/lan9645_phy.c`
- `src/lan9645_serdes.c`
- `src/lan9645_irq.c`
- `src/lan9645_stats.c`
- `src/lan9645_profile_managed_rmii1.c`

---

## 2) Complete Manageable Settings Inventory (what library must cover)

This section lists all control domains relevant for "total control" in managed operation.

### 2.0 Fixed-by-hardware settings (not runtime configurable)
- `SD_125M`: external jumper/strap, required HIGH at reset latch
- `SGPIO_EN`: external jumper/strap, required LOW at reset latch
- `BOOT_MODE[2:0]`, `MGMT_MODE[1:0]`, `JTAG_ENB`: hardware straps sampled at reset

Library rule:
- expose these as read-only profile expectations and startup checks
- do not expose runtime APIs that imply these strap states can be changed by STM32 firmware

### 2.1 Device/global control
- chip reset/restart flow (`nRESET` sequencing)
- SPI SI mode settings (`DEVCPU_GCB::IF_CTRL`, `DEVCPU_GCB::IF_CFGSTAT`)
- device identity/revision readback
- global clock/reset status
- watchdog/error flags
- hardware strap precondition validation (`SD_125M=1`, `SGPIO_EN=0`) as non-runtime parameters

### 2.2 Port multiplexing and interface assignment
- port mux (`HSIO::HW_CFG` family)
- `QSGMII_ENA`
- `RGMII_0_CFG`, `RGMII_1_CFG` (RGMII/RMII assignment)
- interface enable gates:
  - `RMII_ENA`
  - `RGMII_ENA`
  - `GMII_ENA`
- RMII clock source selection (`REF_CLK_SEL`)
- RMII/RGMII TX/RX reset release (`RMII_RX_RST`, `RMII_TX_RST`, `RGMII_RX_RST`, `RGMII_TX_RST`)

### 2.3 Port module and MAC controls (per DEV port)
- reset cycle (`CLOCK_CFG` reset bits)
- link speed (`CLOCK_CFG.LINK_SPEED`)
- duplex (`MAC_MODE_CFG.FDX_ENA`)
- TX/RX enable (`MAC_ENA_CFG.TX_ENA`, `MAC_ENA_CFG.RX_ENA`)
- IFG tuning (`MAC_IFG_CFG`)
- frame length/tag acceptance (`MAC_TAGS_CFG`, related)
- flow control enable/behavior (`MAC_FC_CFG`, FC MAC address)

### 2.4 CuPHY controls (integrated PHY0..PHY4)
- autoneg enable/disable
- advertised capabilities (10/100/1000, full/half)
- forced speed/duplex mode
- EEE enable/disable
- loopback and diagnostics (LinkMD/TDR where required)
- PHY interrupt and link status reads

### 2.5 SerDes / SGMII / SFP controls (DEV5/DEV6)
- SerDes mode selection (SGMII vs 1000BASE-X/SFP path per design)
- ANEG mode for SGMII/PCS
- LOS handling / signal-detect mapping
- equalization/polarity/loopback controls (debug mode)
- per-port enable/reset sequencing

### 2.6 Layer-2 switching behavior
- forwarding database (learn/age policy)
- static MAC entries
- unknown unicast/multicast handling
- flooding/port isolation rules
- CPU copy/redirect rules for selected frame types

### 2.7 VLAN handling
- port VLAN config (`ANA:PORT:VLAN_CFG` class)
- ingress acceptance policy (untagged, C-tag, S-tag, priority-tag)
- default PVID per port
- egress tagging behavior / pop/push policy
- VLAN table membership

### 2.8 QoS and drop precedence
- default QoS class per port
- PCP/DEI mapping to QoS+DP
- DSCP trust/translate maps
- queue scheduling controls (strict/wrr-like policies where applicable)
- ingress/egress drop behavior

### 2.9 Policing and shaping
- ingress policers (rate/burst/action)
- per-stream or per-port policing as needed
- egress shaping options (if used)

### 2.10 CPU/host interaction
- CPU extraction/injection behavior
- CPU queue mapping
- optional header include controls (`SYS::PORT_MODE.INCL_XTR_HDR`, `INCL_INJ_HDR`)
- VRAP related controls (if later used)

### 2.11 Interrupt and event framework
- interrupt source enables
- interrupt status read/ack path
- link-change interrupt routing to `IRQ0`
- sticky bit clear helpers

### 2.12 Statistics and diagnostics
- per-port RX/TX/drop counters (`SYS:STAT:CNT` view mechanism)
- counter clear-shot controls (`STAT_CFG.STAT_CLEAR_SHOT`)
- frame/byte mode selects for selected counters
- health checks and debug snapshots

### 2.13 Time sync / PTP (future-ready)
- timestamp GPIO mapping
- clock mode controls
- per-port timestamp behavior

### 2.14 Security/classification blocks (future phases)
- basic classifier policies
- VCAP IS1/IS2/ES0 rule management
- ACL-like permit/deny actions

---

## 3) Library API Blueprint

### 3.1 Core access API
- `int lan9645_init(lan9645_ctx_t *ctx);`
- `int lan9645_read32(lan9645_ctx_t *ctx, uint32_t reg, uint32_t *val);`
- `int lan9645_write32(lan9645_ctx_t *ctx, uint32_t reg, uint32_t val);`
- `int lan9645_rmw32(lan9645_ctx_t *ctx, uint32_t reg, uint32_t mask, uint32_t val);`
- `int lan9645_wait_field(lan9645_ctx_t *ctx, uint32_t reg, uint32_t mask, uint32_t exp, uint32_t to_ms);`

### 3.2 Board/profile API
- `int lan9645_apply_profile_managed_rmii1(lan9645_ctx_t *ctx);`
- `int lan9645_release_from_reset_and_sync(lan9645_ctx_t *ctx);`

### 3.3 Interface/port API
- `int lan9645_set_rmii1_mode(lan9645_ctx_t *ctx, bool refclk_internal);`
- `int lan9645_set_port_admin(lan9645_ctx_t *ctx, uint8_t dev, bool up);`
- `int lan9645_set_port_speed_duplex(lan9645_ctx_t *ctx, uint8_t dev, lan_speed_t s, bool fdx);`
- `int lan9645_get_port_link(lan9645_ctx_t *ctx, uint8_t dev, lan_link_t *st);`

### 3.4 PHY API
- `int lan9645_phy_set_autoneg(lan9645_ctx_t *ctx, uint8_t phy, bool en);`
- `int lan9645_phy_set_adv(lan9645_ctx_t *ctx, uint8_t phy, lan_phy_adv_t adv);`
- `int lan9645_phy_restart_aneg(lan9645_ctx_t *ctx, uint8_t phy);`

### 3.5 SerDes/SFP API
- `int lan9645_serdes_set_mode(lan9645_ctx_t *ctx, uint8_t sd, lan_serdes_mode_t mode);`
- `int lan9645_serdes_enable(lan9645_ctx_t *ctx, uint8_t sd, bool en);`
- `int lan9645_serdes_get_status(lan9645_ctx_t *ctx, uint8_t sd, lan_serdes_status_t *st);`

### 3.6 VLAN/QoS API (phase 2)
- `int lan9645_vlan_set_port_pvid(...)`
- `int lan9645_vlan_set_membership(...)`
- `int lan9645_qos_set_port_default(...)`
- `int lan9645_qos_set_pcp_map(...)`

### 3.7 IRQ/stats API
- `int lan9645_irq_config(lan9645_ctx_t *ctx, const lan_irq_cfg_t *cfg);`
- `int lan9645_irq_get_and_clear(lan9645_ctx_t *ctx, lan_irq_status_t *st);`
- `int lan9645_stats_read_port(lan9645_ctx_t *ctx, uint8_t dev, lan_port_stats_t *st);`

### 3.8 Health/error API
- `int lan9645_health_snapshot(lan9645_ctx_t *ctx, lan9645_health_t *h);`
- `int lan9645_diag_spi(lan9645_ctx_t *ctx, lan9645_spi_diag_t *d);`
- `int lan9645_diag_links(lan9645_ctx_t *ctx, lan9645_link_diag_t *d);`
- `int lan9645_diag_straps_expectation(const lan9645_profile_t *p, lan9645_strap_expect_t *e);`

---

## 4) Managed Profile: Required Startup Sequence (your board)

1. Hardware reset control:
- assert `LAN_RST` low
- configure STM32 SPI GPIOs
- release `LAN_RST`, wait >= 14 ms

2. SI/SPI readiness:
- write `DEVCPU_GCB::IF_CTRL`
- write `DEVCPU_GCB::IF_CFGSTAT` (padding if needed)

3. Interface mux:
- `HSIO::HW_CFG.QSGMII_ENA = 0`
- set `RGMII_1_CFG` for RMII1 to DEV8
- `RMII_ENA=1`, `RGMII_ENA=0`, `GMII_ENA=0`
- `REF_CLK_SEL=0` (internal 50 MHz output for STM32 `ETH_REF_CLK`)
- `RMII_RX_RST=0`, `RMII_TX_RST=0`

4. Activate only required ports:
- DEV0 (CuPHY0)
- DEV1 (CuPHY1)
- DEV5 (SerDes0)
- DEV6 (SerDes1)
- DEV8 (RMII1 CPU-facing)

5. Configure CuPHY policy:
- initial test profile: 100M full duplex or AN limited to 10/100

6. Configure SerDes policy:
- set mode per module type (SGMII copper or 1000BASE-X optical)

7. Enable LED mux pins:
- GPIO42/43/40/41 to PHY LED functions

8. Enable IRQ0 link event path

---

## 4.1) Error model for user-facing diagnostics

Define one normalized error/event enum used by CLI, logs, and LED status:
- `LAN9645_E_SPI_IFSTAT`
- `LAN9645_E_SPI_TIMEOUT`
- `LAN9645_E_STRAP_EXPECTATION`
- `LAN9645_E_RMII_LINK_TIMEOUT`
- `LAN9645_E_SERDES0_LINK_TIMEOUT`
- `LAN9645_E_SERDES1_LINK_TIMEOUT`
- `LAN9645_E_IRQ_STUCK`
- `LAN9645_E_DROP_COUNTER_ALERT`

Each error should include:
- first timestamp
- last timestamp
- occurrence count
- last context (dev port, register, expected vs actual)

Recovery policy:
- retry for transient SPI/link issues
- controlled `LAN_RST` reinit for persistent transport failures
- hard-stop for strap expectation mismatch

---

## 5) Register Mapping Strategy (important)

This library spec uses symbolic register names from documentation.
For implementation, create a generated register map:
- source: Microchip official CSR definitions/header/XML used by SDK/DSA ecosystem
- output: `lan9645_regs.h` with absolute addresses + field masks

Do not hardcode many literal addresses in application code.
Keep all addresses in one generated file.

---

## 6) Development Roadmap

### Phase 1 (must-have)
- SPI read/write reliable
- RMII1 bring-up
- DEV0/DEV1 link-up
- basic ping through copper ports

### Phase 2
- DEV5/DEV6 SGMII/SFP stability
- IRQ framework
- statistics snapshot API

### Phase 3
- VLAN/QoS helper API
- CPU extraction/injection helpers
- robust error recovery and watchdog handling

### Phase 4
- advanced classifier/VCAP utilities
- PTP/timestamp helper layer

---

## 7) Validation Checklist for Library Completion
- [ ] SPI read/write stress test passes
- [ ] reset/re-init idempotent
- [ ] RMII1 link stable with STM32 ping at 100M
- [ ] CuPHY0 and CuPHY1 both pass ping tests
- [ ] SerDes0 and SerDes1 each pass ping tests
- [ ] IRQ0 events read/clear cleanly
- [ ] stats counters increment and clear as expected
- [ ] no unmanaged strap dependency at runtime

