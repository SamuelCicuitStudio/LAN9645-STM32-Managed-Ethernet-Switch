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
- STM32CubeMX generated firmware baseline: `MxCube 6.17.0`, `FW_H7 v1.13.0`, `LwIP 2.2.1`, `USER_PHY` platform component

---

## 0) Current Firmware Baseline from `firmware/`

This section reflects what is currently generated in `D:\Freelancer\luisspranger\firmware`.

### 0.1 Cube/LwIP profile actually generated
- ETH media interface: `HAL_ETH_RMII_MODE`
- LwIP mode: `NO_SYS=1` (no RTOS)
- `LWIP_DHCP=0` (static IP mode)
- LwIP memory: `MEM_SIZE=10*1024`
- LwIP netconn/socket APIs: disabled (`LWIP_NETCONN=0`, `LWIP_SOCKET=0`)
- Checksum offload path: `CHECKSUM_BY_HARDWARE=1`
- Platform PHY selection: `USER_PHY`
- Cortex-M7 cache state (confirmed on April 4, 2026): `I-Cache=ENABLED`, `D-Cache=ENABLED`

### 0.2 Current generated static IPv4 values
- IP: `192.168.10.50`
- Netmask: `255.255.255.0`
- Gateway: `192.168.10.50`

Managed bring-up policy for phase 1:
- keep IP and netmask above
- set gateway to `0.0.0.0` for direct/isolated ping tests

### 0.3 Generated integration gaps to close in implementation
1. `MX_LWIP_Process()` exists but is not yet called in the `while(1)` loop.
2. `ethernet_link_check_state(struct netif *netif)` is currently empty.
3. ETH DMA sections are declared in `ethernetif.c` (`.RxDescripSection`, `.TxDescripSection`, `.Rx_PoolSection`) but linker scripts still need explicit section placement rules.
4. SPI1 is currently configured as `SPI_DATASIZE_4BIT`; LAN9645 SI transport layer must use byte/word framing compatible with register transactions (8-bit stream handling in HAL transport implementation).

### 0.4 LwIP documentation constraints reflected in this spec
Based on STM32Cube LwIP usage model (UM1713 class workflow for `NO_SYS=1`):
- `MX_LWIP_Process()` must run continuously in main loop.
- link-status maintenance is performed via periodic `ethernet_link_check_state()` calls.
- timeout engine (`sys_check_timeouts`) is mandatory in no-RTOS mode.
- `sys_now()` must provide millisecond tick source.

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

5. LwIP adapter layer (`lan9645_lwip_adapter.c`)
- bridges LAN9645 managed link state to STM32 `netif` link/admin state
- owns the USER_PHY adaptation contract for Cube-generated middleware
- provides periodic service hook for `MX_LWIP_Process()` / link refresh

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
- `src/lan9645_lwip_adapter.c`
- `src/lan9645_user_phy_adapter.c`

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
- advertised capabilities (10/100 only, full/half; 1000 disabled)
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

### 2.15 LwIP/USER_PHY integration controls (current firmware contract)
- ownership of link state source for `gnetif`
- mapping between LAN9645 `DEV8` link/speed/duplex and LwIP/HAL ETH state
- handling of Cube `USER_PHY` abstraction (`eth_custom_phy_interface.*`)
- periodic no-RTOS processing contract (`MX_LWIP_Process`)
- static IP profile validation at startup

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

### 3.9 LwIP/USER_PHY adapter API (new, required)
- `int lan9645_lwip_adapter_init(struct netif *netif, lan9645_ctx_t *ctx);`
- `void lan9645_lwip_adapter_poll(struct netif *netif);`
- `int lan9645_lwip_set_static_ipv4(struct netif *netif, uint8_t ip[4], uint8_t mask[4], uint8_t gw[4]);`
- `int lan9645_user_phy_readreg(uint32_t dev, uint32_t reg, uint32_t *val);`
- `int lan9645_user_phy_writereg(uint32_t dev, uint32_t reg, uint32_t val);`

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
- locked profile for `DEV0` and `DEV1`:
  - autoneg enabled
  - advertise only `10HD`, `10FD`, `100HD`, `100FD`
  - do not advertise 1000M capability
  - default bring-up target: 100M full duplex if partner supports it

6. Configure SerDes policy:
- set mode per module type (SGMII copper or 1000BASE-X optical)

7. Enable LED mux pins:
- GPIO42/43/40/41 to PHY LED functions

8. Enable IRQ0 link event path

9. Initialize LwIP netif with static IPv4 profile
- phase-1 profile: `192.168.10.50/24`, gateway `0.0.0.0`

10. Ensure no-RTOS LwIP service loop is running
- call `MX_LWIP_Process()` continuously in main loop

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

## 4.2) USER_PHY integration rule (based on generated firmware)

Cube currently generates `eth_custom_phy_interface.c/.h` for `USER_PHY`.
For this project:
- USER_PHY is an adapter boundary, not a real external PHY chip driver.
- authoritative link/speed state must come from LAN9645 managed status (`DEV8` path), not from standalone LAN8742-style assumptions.

Implementation rule:
1. Bind USER_PHY read/write callbacks to LAN9645-backed accessors.
2. `ethernet_link_check_state()` must consume LAN9645 link status and update netif link state deterministically.
3. USER_PHY register semantics can be emulated minimally for middleware compatibility, but LAN9645 managed APIs remain the source of truth.

---

## 4.3) Locked Phase-1 Runtime Contract (No Optional Paths)

This section is strict for phase-1 firmware implementation.

### 4.3.1 USER_PHY behavior (exact)
1. `USER_PHY` is implemented as a LAN9645 adapter layer.
2. Supported PHY semantics for phase 1 are limited to:
- `BCR`, `BSR`, `ANAR`, `ANLPAR`, `PHYI1R`, `PHYI2R` compatibility handling.
3. Copper advertisement policy is fixed:
- advertise only `10HD`, `10FD`, `100HD`, `100FD`.
- 1000M advertisement is not supported and must remain disabled.
4. If a request attempts to enable 1000M advertisement, return an explicit error.
5. Netif link status source for LwIP is LAN9645 managed link status (`DEV8` path), not an external PHY state machine.

### 4.3.2 LwIP operational profile (exact)
1. `NO_SYS=1` mode.
2. `DHCP=0` static addressing mode.
3. Static IPv4 profile:
- IP `192.168.10.50`
- netmask `255.255.255.0`
- gateway `0.0.0.0`
4. `MX_LWIP_Process()` must run continuously in the main loop.
5. `ethernet_link_check_state()` must run on 100 ms cadence.

### 4.3.3 CLI command set (exact phase-1 list)
- `help`
- `version`
- `status all`
- `status errors`
- `reset lan`
- `reg read <addr>`
- `reg write <addr> <value>`
- `reg rmw <addr> <mask> <value>`
- `link show`
- `phy show <0|1>`
- `phy adv set <0|1> <10hd|10fd|100hd|100fd>`
- `phy mode set <0|1> <aneg|force10hd|force10fd|force100hd|force100fd>`
- `serdes show <0|1>`
- `stats show <dev>`
- `stats clear <dev>`
- `console owner usb`
- `console owner uart`
- `console owner release`
- `bridge usb_uart on`
- `bridge usb_uart off`
- `bridge status`
- `debug line show`
- `time show`

### 4.3.4 USB/UART behavior (exact)
1. USB CDC is primary CLI transport.
2. Boot transport rule:
- wait up to 3 s for USB enumeration.
- if enumerated: CLI owner = USB.
- if not enumerated after 3 s: CLI owner = UART4.
3. Command input is accepted only from current owner.
4. All runtime errors are forwarded only to current owner channel (`USB` or `UART4`).
5. If owner is `NONE`, errors are forwarded to both channels until an owner is acquired.
6. The current owner channel is the active debug serial line.
7. If USB enumerates later, owner switches to USB automatically.
8. `bridge usb_uart on` behavior:
- USB RX bytes are forwarded to UART TX.
- UART RX bytes are forwarded to USB TX.
- CLI parser is suspended while bridge is ON.
- bridge exit sequence is `+++` followed by 1 s idle.

### 4.3.5 LED behavior (exact)
1. `HEART` (`PG6`, active high): 1 Hz blink after firmware main loop starts.
2. `STATUS` (`PG7`, active high):
- OFF during startup/config.
- ON only when LAN init completed and `DEV8` link is up.
- OFF if `DEV8` link remains down for >2 s.
3. `ERR` (`PG8`, active high):
- OFF when healthy.
- blink code 1: SPI/SI comm error
- blink code 2: strap mismatch
- blink code 3: RMII link failure
- blink code 4: SerDes link failure
- blink code 5: IRQ handling error
- steady ON on fatal unrecoverable error.

### 4.3.6 USER button behavior (exact)
1. Button is active-low (`PI10`) with 30 ms debounce.
2. Short press (80 ms to <3 s): execute `status all`.
3. Long press (>=3 s): execute LAN re-initialization sequence.
4. No additional button functions are allowed in phase 1.

### 4.3.7 LAN9645 communication fault routing (exact)
This subsection is mandatory for all LAN9645<->STM32 communication faults.

1. Mandatory LAN communication error IDs:
- `LAN9645_E_SPI_IFSTAT`: `DEVCPU_GCB::IF_CFGSTAT.IF_STAT=1` after SI read.
- `LAN9645_E_SPI_TIMEOUT`: SPI SI transaction timeout or invalid response window exceeded.
- `LAN9645_E_STRAP_EXPECTATION`: runtime behavior inconsistent with locked strap profile.
- `LAN9645_E_IRQ_STUCK`: `IRQ0` asserted with no clearable interrupt source.
- `LAN9645_E_DROP_COUNTER_ALERT`: persistent RX/TX/drop counter anomaly during active traffic.

2. Mandatory serial forwarding rule:
- always emit LAN communication errors on the current CLI owner channel (`USB` or `UART4`).
- if owner is `NONE`, mirror the same error line to both USB CDC and UART4.
- this routing rule is not optional and is the required debug-line behavior.

3. Mandatory error log line format:
- `[timestamp][ERR][LAN][<ERROR_ID>] <text>; dev=<dev>; reg=<addr>; ctx=<context>`
- example:
  - `[2026-04-04 14:32:10][ERR][LAN][LAN9645_E_SPI_TIMEOUT] SPI timeout; dev=8; reg=IF_CFGSTAT; ctx=read`

4. Mandatory runtime handling:
- `LAN9645_E_SPI_IFSTAT`:
  - clear `IF_STAT`.
  - retry the failed SI read/write up to 3 times.
  - if still failing, escalate to `LAN9645_E_SPI_TIMEOUT`.
- `LAN9645_E_SPI_TIMEOUT`:
  - retry transaction up to 3 times.
  - if still failing, pulse `LAN_RST` and execute full LAN re-init.
  - if re-init fails, set `ERR` LED steady ON and hold in degraded state.
- `LAN9645_E_STRAP_EXPECTATION`:
  - do not continue LAN bring-up.
  - set `ERR` LED steady ON and emit persistent fault line every 2 s.
- `LAN9645_E_IRQ_STUCK`:
  - read/clear interrupt status path once.
  - if stuck condition remains, perform LAN re-init and report fault counter.

5. Mandatory CLI observability for LAN faults:
- `status errors` must show, for each LAN error ID:
  - occurrence count
  - first timestamp
  - last timestamp
  - last context
- `debug line show` must display current owner (`USB`, `UART4`, or `NONE`) and whether mirroring is active.

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
- LwIP no-RTOS loop stable (`MX_LWIP_Process`)
- USER_PHY adapter path tied to LAN9645 link source
- static IP profile verified with gateway policy for direct test bench

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
- [ ] `ethernet_link_check_state()` updates netif correctly from LAN9645 status
- [ ] `MX_LWIP_Process()` is executed in main loop (no RTOS mode)
- [ ] ETH DMA descriptor/pool linker sections are mapped explicitly in linker script
- [ ] CuPHY advertisement is restricted to 10/100 only (1000 disabled)
- [ ] USB primary CLI ownership and fallback-to-UART behavior matches section 4.3.4
- [ ] LED and USER button behavior matches section 4.3.5 and 4.3.6

