# LAN9645 Managed Bring-Up and Ping Test Configuration (STM32)

## Goal
Bring up LAN9645 in managed mode (SPI control from STM32), then run ping tests from STM32 through:
- CuPHY port 0
- CuPHY port 1
- SGMII/SFP port on SerDes0
- SGMII/SFP port on SerDes1

This file defines what to configure in LAN9645 and in what order.

## Design Assumptions (from current project)
- Package: LAN9645 128-TQFP
- Boot straps:
  - `BOOT_MODE[2:0] = 000`
  - `MGMT_MODE[1:0] = 11` (SPI client)
  - `JTAG_ENB = 1`
  - `SD_125M = 1`
  - `SGPIO_EN = 0`
- STM32 interface to LAN9645: RMII1 + SPI + `LAN_RST` + `IRQ0`
- SPI pins:
  - `SI_nCS`: pin 98
  - `SI_CLK`: pin 97
  - `SI_DI`: pin 100
  - `SI_DO`: pin 99

## Source References
- LAN9645xS DS00006066C:
  - Section 4.21 (reset/clocking)
  - Section 4.22.1.1 (SI/SPI client mode)
  - Section 4.5/4.6 (RGMII/RMII register controls)
  - Section 4.7.1 (MAC/port module controls)
- LAN96459F checklist DS00006273B:
  - Section 6 (port multiplexing via `HSIO::HW_CFG`)
  - Section 8 (RMII config checklist)
  - Section 9.2 (SPI pin mapping)

## 1) SPI Access Layer (must work first)

### 1.1 SPI protocol format
SI implements SPI-compatible register access:
- transfer starts when `SI_nCS` asserted
- shift: `RW(1 bit) + SI_ADDR(23 bits) + DATA(32 bits)`
- address conversion:
  - `SI_ADDR = (REG_ADDR & 0x01FFFFFF) >> 2`

### 1.2 SI interface setup registers
Configure after each hard reset:
- `DEVCPU_GCB::IF_CTRL`
  - controls endianness and bit order
- `DEVCPU_GCB::IF_CFGSTAT`
  - `IF_CFG`: padding bytes for read latency handling

Recommended for STM32 bring-up:
- start with conservative SI clock or 1 dummy byte padding
- read-modify-write tests on a known register until stable

## 2) Top-Level Interface and Port-Mux Configuration

Configure these fields before enabling traffic:

| Register/Field | Target value | Purpose |
|---|---|---|
| `HSIO::HW_CFG.QSGMII_ENA` | `0` | disable QSGMII mode |
| `HSIO::HW_CFG.RGMII_1_CFG` | map to `DEV8` as RMII1 path | route RMII1 to CPU-facing port module |
| `RMII_ENA` | `1` | enable RMII GPIO interface |
| `RGMII_ENA` | `0` | disable RGMII when using RMII |
| `GMII_ENA` | `0` | keep GMII disabled |
| `REF_CLK_SEL` | `0` (internal) or `1` (external) | 50 MHz RMII REF clock source |
| `RMII_RX_RST` | `0` | release RMII RX reset |
| `RMII_TX_RST` | `0` | release RMII TX reset |

Notes:
- For your current wiring (`ETH_REF_CLK` from LAN9645 to STM32), use `REF_CLK_SEL=0` internal.
- RMII1 corresponds to the GPIO group you already wired (`GPIO_4..13` subset).

## 3) Port Module Configuration (active vs unused)

### 3.1 Active ports for this project
- `DEV0` = CuPHY0
- `DEV1` = CuPHY1
- `DEV5` = SerDes0 (SGMII/SFP)
- `DEV6` = SerDes1 (SGMII/SFP)
- `DEV8` = RMII1 to STM32

### 3.2 Per-active-port required controls
For each active `DEVx`, configure the MAC/port module registers:

| Register/Field | Required setting |
|---|---|
| `CLOCK_CFG` resets | run reset sequence (set reset bits, then clear reset bits) |
| `MAC_ENA_CFG.TX_ENA` | `1` |
| `MAC_ENA_CFG.RX_ENA` | `1` |
| `MAC_MODE_CFG.FDX_ENA` | `1` for initial full-duplex bring-up |

For `DEV8` (RMII to STM32):
- set `CLOCK_CFG.LINK_SPEED = 100 Mbps`
- STM32 ETH must also be set to 100 Mbps full duplex for first tests

### 3.3 Unused ports
Keep unused ports (`DEV2`, `DEV3`, `DEV4`, `DEV7`) disabled for first bring-up:
- keep TX/RX disabled and/or keep port reset asserted
- this reduces debug noise and interrupt load

## 4) CuPHY0/CuPHY1 test policy (10/100 target)

In managed mode, do not rely on unmanaged straps for runtime behavior.
For first ping validation:
- force/limit link behavior on CuPHY0 and CuPHY1 to 100M full duplex (or AN with only 10/100 advertised)
- ensure STM32-side port (`DEV8`) stays fixed at 100M full duplex

Implementation path:
- use LAN9645 PHY control path (MIIM/controller side) to set PHY advertisement/speed policy for port 0/1
- keep this as explicit initialization in STM32 firmware

## 5) SGMII/SFP ports (DEV5/DEV6)

For each SerDes port used in test:
- configure corresponding SerDes/PCS mode for your module type:
  - SGMII copper module, or
  - 1000BASE-X optical SFP
- release related resets and enable TX/RX on corresponding `DEV5`/`DEV6`
- wait for link-up status before ping test

Practical rule:
- bring up only one SGMII/SFP port first (DEV5), validate ping, then enable DEV6.

## 6) LED and GPIO mux after boot

After strap latch and core setup:
- configure GPIO mux for LED outputs:
  - `GPIO_42` -> `PHY0_LED0`
  - `GPIO_43` -> `PHY0_LED1`
  - `GPIO_40` -> `PHY1_LED0`
  - `GPIO_41` -> `PHY1_LED1`
- keep external strap resistors as designed; muxing takes effect after boot

## 7) Interrupt setup (optional but recommended)

- configure LAN9645 interrupt controller sources for link-change events
- route expected summary/source to `IRQ0` (GPIO_33)
- STM32 EXTI handler should read/clear interrupt source registers

## 8) STM32 Ping Test Program Plan

### 8.1 Bring-up sequence in firmware
1. Hold LAN reset low, init STM32 SPI + GPIO.
2. Release `LAN_RST`, wait >=14 ms.
3. Configure `IF_CTRL` / `IF_CFGSTAT` (SI interface behavior).
4. Apply top-level HW config (`HSIO::HW_CFG`, RMII fields, resets).
5. Configure active DEV ports (`DEV0`, `DEV1`, `DEV5`, `DEV6`, `DEV8`).
6. Configure CuPHY speed policy for port0/1.
7. Configure SGMII/SFP mode for DEV5/DEV6.
8. Enable LED mux and optional IRQ.
9. Start STM32 LWIP stack on RMII interface.

### 8.2 Ping test matrix
- Test A: Copper Port 0
  - connect PC to CuPHY0, static IP same subnet, ping STM32 IP
- Test B: Copper Port 1
  - connect PC to CuPHY1, ping STM32 IP
- Test C: SGMII/SFP Port 0 (DEV5)
  - connect SFP link partner, verify link, ping STM32 IP
- Test D: SGMII/SFP Port 1 (DEV6)
  - same as Test C

Record for each test:
- link up/down status
- negotiated speed/duplex
- ping success %, RTT
- LED behavior

## 9) Minimal register write checklist (implementation-ready)

1. `DEVCPU_GCB::IF_CTRL` -> set SPI format used by STM32
2. `DEVCPU_GCB::IF_CFGSTAT.IF_CFG` -> set read padding if needed
3. `HSIO::HW_CFG.QSGMII_ENA = 0`
4. `HSIO::HW_CFG.RGMII_1_CFG` -> RMII1/DEV8 mapping
5. `RMII_ENA = 1`, `RGMII_ENA = 0`, `GMII_ENA = 0`
6. `REF_CLK_SEL = 0` (internal 50 MHz output)
7. `RMII_RX_RST = 0`, `RMII_TX_RST = 0`
8. For each active `DEVx`: `CLOCK_CFG` reset cycle
9. For each active `DEVx`: `MAC_ENA_CFG.TX_ENA=1`, `MAC_ENA_CFG.RX_ENA=1`
10. `DEV8 CLOCK_CFG.LINK_SPEED = 100 Mbps`
11. CuPHY0/1 PHY advertisement/speed config (10/100 test policy)
12. SGMII/SFP mode + enable for `DEV5/DEV6`

## 10) Notes on addresses
This file intentionally uses symbolic register names from Microchip documentation.
Use your register header/CSR map (or generated register definitions) in STM32 code to map symbolic names to absolute addresses.

