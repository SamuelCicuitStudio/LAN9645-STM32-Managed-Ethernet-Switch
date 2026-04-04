# STM32H743 LwIP Activation and PHY Driver Choice (LAN9645 Project)

## Scope
This document removes confusion about what to enable in STM32CubeMX for this project:
- STM32H743 as RMII MAC host
- LAN9645 managed switch configured over SPI
- Ping-first firmware bring-up with `HAL ETH + LwIP`

---

## 1) Final Stack Decision

For first bring-up:
- Enable `ETH`
- Enable `LwIP`
- Keep `MbedTLS` **disabled**
- Keep `FreeRTOS` **disabled** (NO_SYS mode)

Why:
- Ping/ARP/IP tests do not require TLS.
- In CubeMX, FreeRTOS becomes mandatory only when MbedTLS is enabled.

---

## 2) What To Activate in CubeMX (Checklist)

## 2.1 ETH peripheral
- Media interface: `RMII`
- Keep ETH IRQ enabled
- Use your board pin mapping for RMII signals

## 2.2 Cortex-M7/cache
- Enable `I-Cache`
- Enable `D-Cache` (required by CubeMX condition on H7 for LwIP activation)

## 2.3 LwIP middleware
- Enable `LwIP`
- Select `NO_SYS = 1` (no RTOS mode) for initial phase
- Keep only needed protocols first:
  - IPv4
  - ARP
  - ICMP (for ping)
- Optional later: UDP/TCP sockets/netconn if needed

## 2.4 Middleware not required now
- `MbedTLS`: disabled
- `FreeRTOS`: disabled

## 2.5 Keep for diagnostics
- USB Device CDC (CLI transport)
- UART4 (fallback CLI)

---

## 3) PHY Driver Selection in Your Case

Use: **`Undefined` / `User-defined PHY`**  
Do **not** use `LAN8742`.

Reason:
- `LAN8742` driver is for a specific external PHY over MDC/MDIO.
- Your data path uses LAN9645 switch CPU-facing RMII interface and managed control over SPI.
- Link/config truth comes from LAN9645 managed registers (SPI library), not from a LAN8742 PHY model.

If CubeMX asks for PHY component:
- choose `Undefined` (or `User-defined`)
- implement board-specific link-state handling
- map link status from LAN9645 SPI diagnostics into LwIP netif state

---

## 4) Critical RAM/Linker/Cache Rules (H7 + ETH DMA)

For stable Ethernet RX/TX:
1. Put ETH DMA descriptors in DMA-accessible RAM section.
2. Put ETH RX/TX buffers in DMA-accessible RAM section.
3. Ensure linker defines these sections explicitly.
4. Ensure cache coherency:
- non-cacheable MPU region for ETH buffers, or
- proper D-Cache clean/invalidate around DMA ownership transitions.

Typical sections:
- `.RxDecripSection`
- `.TxDecripSection`
- `.RxArraySection` / `.TxArraySection` (or equivalent)

If this is wrong, symptoms include:
- intermittent ping loss
- corrupted frames
- RX/TX freeze

---

## 5) Integration Direction (No Code Yet)

1. Bring up LAN9645 over SPI first (reset, straps precheck, RMII/port config).
2. Bring up ETH + LwIP on STM32.
3. Report link health from LAN9645 SPI status into CLI.
4. Run ping matrix (Cu0, Cu1, SFP0, SFP1).

---

## 6) Quick Decision Table

| Item | Selection |
|---|---|
| ETH | Enabled (RMII) |
| LwIP | Enabled |
| LwIP mode | `NO_SYS` (initial) |
| FreeRTOS | Disabled (initial) |
| MbedTLS | Disabled (initial) |
| PHY driver in CubeMX | `Undefined` / `User-defined` |
| LAN8742 driver | Not used |

