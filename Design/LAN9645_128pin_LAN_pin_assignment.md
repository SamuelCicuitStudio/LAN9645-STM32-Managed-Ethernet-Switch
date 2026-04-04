# LAN9645 (128-TQFP) LAN Pin + Strap Plan (Precise)

## Locked Project Targets
- Device: LAN9645xS / LAN96459F, 128-pin TQFP
- Copper ports used: CuPHY0 and CuPHY1
- Copper speed target: 10/100 Mbps operation (managed mode configuration over SPI)
- SerDes used: SGMII0 and SGMII1
- External 125 MHz SerDes reference clock used
- Host MCU: STM32H743 (MAC), connected by RMII
- Management interface: SPI (STM32 SPI host -> LAN9645 SPI client)
- Copper LEDs: enabled (including strap-overlap pins where needed)

## Source Basis
- LAN9645xS Data Sheet DS00006066C
  - Section 3.1.1 (128-pin assignments)
  - Section 3.3.1 (managed-mode GPIO alt functions)
  - Section 3.4 (strap definitions)
- LAN96459F Hardware Design Checklist DS00006273B
  - Section 4 (straps + LED overlap)
  - Section 8 (RGMII/RMII)
  - Section 9.2 (SPI pin options)

## Interface Pin Map (Locked)

### CuPHY0 / CuPHY1 differential pairs
| Function | TQFP Pins |
|---|---|
| CuPHY0 (P0_AP/AN/BP/BN/CP/CN/DP/DN) | 102, 103, 105, 106, 108, 109, 110, 111 |
| CuPHY1 (P1_AP/AN/BP/BN/CP/CN/DP/DN) | 112, 113, 114, 115, 117, 118, 120, 121 |

### SGMII interfaces
| Function | TQFP Pins |
|---|---|
| SGMII0 (S0_CLKP, S0_TXN/P, S0_RXN/P) | 45, 46, 48, 49, 50 |
| SGMII1 (S1_TXN/P, S1_RXN/P) | 53, 55, 56, 57 |

### 125 MHz SerDes reference clock (hardware strap/jumper controlled)
Use an external low-jitter 125 MHz oscillator.
In this design, `SD_125M` and `SGPIO_EN` are selected by external jumper/strap hardware (not by STM32 runtime GPIO control).

| Net | From | To | Notes |
|---|---|---|---|
| `SERDES_125M_CLK` | 125 MHz oscillator output | LAN9645 `S0_CLKP` (pin 45) | Clock input for SerDes reference mode |
| `SD_125M_SEL` | External strap/jumper | LAN9645 `GPIO_23/SD_125M` (pin 71) | Set HIGH for 125 MHz mode |
| `SGPIO_EN_SEL` | External strap/jumper | LAN9645 `GPIO_22/SGPIO_EN` (pin 72) | Must be LOW when `SD_125M=1` |

Recommended startup behavior:
- Keep `SERDES_125M_CLK` valid before releasing LAN9645 `nRESET`, because straps are latched at reset release.
- Keep jumper/strap logic fixed at boot:
  - `SD_125M=1`
  - `SGPIO_EN=0`
- Do not source `SERDES_125M_CLK` directly from STM32 MCO.

### RMII used for STM32H743: RMII1 on GPIO4..13 (recommended)
Reason: avoids overlap with critical managed straps on GPIO24..28 (BOOT_MODE/MGMT_MODE).

| STM32 ETH Signal | LAN9645 Signal (RMII1) | GPIO | TQFP Pin | Direction |
|---|---|---:|---:|---|
| ETH_TXD0 | RGMII1_RXD0 | GPIO_4 | 95 | STM32 -> LAN |
| ETH_TXD1 | RGMII1_RXD1 | GPIO_5 | 94 | STM32 -> LAN |
| ETH_TX_EN | RGMII1_RXCTL_CRS_DV | GPIO_9 | 89 | STM32 -> LAN |
| ETH_RXD0 | RGMII1_TXD0 | GPIO_12 | 85 | LAN -> STM32 |
| ETH_RXD1 | RGMII1_TXD1 | GPIO_13 | 83 | LAN -> STM32 |
| ETH_CRS_DV | RGMII1_TXCTL_TX_DV | GPIO_11 | 86 | LAN -> STM32 |
| ETH_REF_CLK | RGMII1_TXC_REFCLK | GPIO_10 | 87 | LAN -> STM32 (50 MHz in internal-clock mode) |

RMII notes:
- `RGMII1_RXD2_RXER` (GPIO_6, pin 93) is optional in RMII mode. STM32H743 RMII set you listed does not require it.
- `RGMII1_RXC_REFCLK` (GPIO_8, pin 90) is not used in the selected internal REF_CLK output mode.

### SPI management (selected pin group)
Use GPIO[0:3] (keeps GPIO40/41 free for PHY1 LEDs).

| SPI Signal | GPIO | TQFP Pin | Alt |
|---|---:|---:|---:|
| SI_nCS | GPIO_2 | 98 | ALT1 |
| SI_CLK | GPIO_3 | 97 | ALT1 |
| SI_DI (MOSI into LAN9645) | GPIO_0 | 100 | ALT1 |
| SI_DO (MISO out of LAN9645) | GPIO_1 | 99 | ALT1 |

STM32 mapping for control SPI:
- STM32 `SPIx_NSS` -> LAN `SI_nCS` (pin 98)
- STM32 `SPIx_SCK` -> LAN `SI_CLK` (pin 97)
- STM32 `SPIx_MOSI` -> LAN `SI_DI` (pin 100)
- STM32 `SPIx_MISO` <- LAN `SI_DO` (pin 99)

### Additional STM32 control lines
| Function | LAN9645 Pin | GPIO | TQFP Pin | Direction |
|---|---|---:|---:|---|
| LAN_RST | nRESET | - | 101 | STM32 -> LAN (active-low reset) |
| IRQ0 | IRQ0 alt function on GPIO_33 | GPIO_33 | 60 | LAN -> STM32 EXTI |

IRQ0 note:
- `GPIO_33` is also strap `EEPROM_SIZE` at reset latch; keep its strap resistor and still use IRQ0 after boot.

LAN_RST note:
- Keep `nRESET` pulled up to `VDD_IO_A` (for example 100k pull-up) and optionally 0.1 uF to GND, as in checklist guidance.

## Exact Startup Strap Plan (Pin -> GND/VCC)

Notes:
- `VCC` means the **matching VDD_IO domain rail** for that GPIO bank.
- If all VDD_IO rails are 3.3V in your design, then every `VCC` below is 3.3V.
- Strap resistors shown are recommended external defaults for this project.
- Managed-mode policy used here:
  - Keep only required straps HIGH.
  - Pull all other unused/unmanaged strap inputs LOW for deterministic startup.

### Managed-mode critical straps (must be fixed)
| Strap | GPIO / Pin | Value | Connect at startup |
|---|---|---:|---|
| BOOT_MODE0 | GPIO_24 / pin 70 | 0 | 10k to GND |
| BOOT_MODE1 | GPIO_25 / pin 68 | 0 | 10k to GND |
| BOOT_MODE2 | GPIO_26 / pin 67 | 0 | 10k to GND |
| MGMT_MODE0 | GPIO_27 / pin 66 | 1 | 10k to VCC (`VDD_IO_C`) |
| MGMT_MODE1 | GPIO_28 / pin 65 | 1 | 10k to VCC (`VDD_IO_C`) |
| JTAG_ENB | GPIO_34 / pin 43 | 1 | 10k to VCC (`VDD_IO_E`) |
| SD_125M | GPIO_23 / pin 71 | 1 | 10k to VCC (`VDD_IO_C`) |

Resulting key values:
- `BOOT_MODE[2:0] = 000` (managed, no internal ROM unmanaged boot)
- `MGMT_MODE[1:0] = 11` (SPI client)
- `JTAG_ENB = 1` (JTAG disabled)
- `SD_125M = 1` (external 125 MHz SerDes reference enabled)

### Additional straps fixed for deterministic startup
(These are unmanaged-feature straps; still tied explicitly.)

| Strap | GPIO / Pin | Value | Connect at startup |
|---|---|---:|---|
| SGPIO_EN | GPIO_22 / pin 72 | 0 | 10k to GND (must be 0 when `SD_125M=1`) |
| LOOP_EN | GPIO_35 / pin 42 | 0 | 10k to GND |
| E2E_CLOCK_EN | GPIO_10 / pin 87 | 0 | 47k to GND |
| FLOW_EN | GPIO_11 / pin 86 | 0 | 47k to GND |
| EEE_EN | GPIO_12 / pin 85 | 0 | 47k to GND |
| LED_MODE | GPIO_13 / pin 83 | 0 | 47k to GND |
| CUPHY_MODE | GPIO_14 / pin 82 | 0 | 47k to GND |
| SFP_EN | GPIO_15 / pin 81 | 0 | 47k to GND |
| EEPROM_SIZE | GPIO_33 / pin 60 | 0 | 22k to GND (`VDD_IO_D` domain is 3.3V) |
| GPIO_MODE0 | GPIO_36 / pin 41 | 0 | 10k to GND |
| GPIO_MODE1 | GPIO_37 / pin 40 | 0 | 10k to GND |
| SD_MODE0 | GPIO_39 / pin 36 | 0 | 10k to GND |
| SD_MODE1 | GPIO_40 / pin 35 | 0 | 10k to GND |
| XMII_MODE0 | GPIO_41 / pin 34 | 0 | 10k to GND |
| XMII_MODE1 | GPIO_38 / pin 38 | 0 | 10k to GND |

Why 47k on GPIO10..15: these pins are used by RMII1 data/clock after reset, so weaker straps reduce dynamic loading.
Why 22k on GPIO33: still robust low strap for `EEPROM_SIZE=0`, but lighter load when GPIO_33 is used as IRQ0 output.
Critical: if `SD_125M=1`, do not tie `SGPIO_EN` high. `SGPIO_EN` must be strapped low.
Critical: your previous note saying `SGPIO_EN` connected to +3V3 conflicts with `SD_125M=1`; this must be changed to GND strap.
Note: `CUPHY_MODE` is strapped low here because it is unmanaged-only and your policy is pull-down for unused strap features. In managed mode, copper behavior is set by SPI software configuration.

### Definitive bootstrap matrix (managed SPI + RMII1 + 125 MHz SerDes)
Control source note:
- `SD_125M` and `SGPIO_EN` states are provided by external jumper hardware in this project.
- STM32 firmware does not change these strap states at runtime.

| Strap | GPIO | TQFP pin | Used in this mode? | Required state | Managed-mode note |
|---|---:|---:|---|---|---|
| BOOT_MODE0 | 24 | 70 | USED (mandatory) | LOW | `BOOT_MODE[2:0]=000` |
| BOOT_MODE1 | 25 | 68 | USED (mandatory) | LOW | `BOOT_MODE[2:0]=000` |
| BOOT_MODE2 | 26 | 67 | USED (mandatory) | LOW | `BOOT_MODE[2:0]=000` |
| MGMT_MODE0 | 27 | 66 | USED (mandatory) | HIGH | `MGMT_MODE[1:0]=11` for SPI client |
| MGMT_MODE1 | 28 | 65 | USED (mandatory) | HIGH | `MGMT_MODE[1:0]=11` for SPI client |
| JTAG_ENB | 34 | 43 | USED (mandatory) | HIGH | JTAG disabled |
| SD_125M | 23 | 71 | USED (mandatory) | HIGH | external 125 MHz SerDes ref enabled |
| SGPIO_EN | 22 | 72 | USED (constraint) | LOW | must be LOW when `SD_125M=1` |
| LOOP_EN | 35 | 42 | UNUSED (strapped) | LOW | unmanaged feature, forced low |
| E2E_CLOCK_EN | 10 | 87 | UNUSED (strapped) | LOW | unmanaged feature, pin reused by RMII1 |
| FLOW_EN | 11 | 86 | UNUSED (strapped) | LOW | unmanaged feature, pin reused by RMII1 |
| EEE_EN | 12 | 85 | UNUSED (strapped) | LOW | unmanaged feature, pin reused by RMII1 |
| LED_MODE | 13 | 83 | UNUSED (strapped) | LOW | unmanaged feature, pin reused by RMII1 |
| CUPHY_MODE | 14 | 82 | UNUSED (strapped) | LOW | unmanaged feature, forced low |
| SFP_EN | 15 | 81 | UNUSED (strapped) | LOW | unmanaged feature, forced low |
| EEPROM_SIZE | 33 | 60 | UNUSED (strapped) | LOW | unmanaged EEPROM strap; pin later used as IRQ0 |
| GPIO_MODE0 | 36 | 41 | UNUSED (strapped) | LOW | unmanaged GPIO mode strap |
| GPIO_MODE1 | 37 | 40 | UNUSED (strapped) | LOW | unmanaged GPIO mode strap |
| SD_MODE0 | 39 | 36 | UNUSED (strapped) | LOW | unmanaged SerDes mode strap |
| SD_MODE1 | 40 | 35 | UNUSED (strapped) | LOW | unmanaged SerDes mode strap; also PHY1_LED0 pin |
| XMII_MODE0 | 41 | 34 | UNUSED (strapped) | LOW | unmanaged XMII strap; also PHY1_LED1 pin |
| XMII_MODE1 | 38 | 38 | UNUSED (strapped) | LOW | unmanaged XMII strap |

## Managed-mode RMII bring-up targets (software)
After reset latch and SPI access is available:
- Assign RMII1 interface to the intended internal port module using `RGMII_1_CFG` RMII selection.
- Enable RMII on GPIOs (`RMII_ENA=1`).
- Keep RGMII mode disabled for this interface (`RGMII_ENA=0` for the RMII-used interface path).
- Set `REF_CLK_SEL` for internal 50 MHz output mode when LAN drives STM32 `ETH_REF_CLK`.
- Release RMII RX/TX reset controls (`RMII_RX_RST=0`, `RMII_TX_RST=0`).

## Copper LED Pin Plan (with strap overlap handled)

### Validation of your selected pins (documentation cross-check)
Your chosen LED pins are correct for CuPHY0/CuPHY1 in 128-TQFP:
- `PHY0_LED1` -> pin 31 (`GPIO_43`, ALT2)
- `PHY0_LED0` -> pin 33 (`GPIO_42`, ALT2)
- `PHY1_LED1` -> pin 34 (`GPIO_41`, ALT2)
- `PHY1_LED0` -> pin 35 (`GPIO_40`, ALT2)

This matches:
- DS00006066C pin map (`GPIO_43` pin 31, `GPIO_42` pin 33, `GPIO_41` pin 34, `GPIO_40` pin 35)
- DS00006273B Table 4-2 LED names and ALT mappings

Important overlap check (also documented in DS00006273B Table 4-3):
- pin 35 (`GPIO_40`) overlaps strap `SD_MODE1`
- pin 34 (`GPIO_41`) overlaps strap `XMII_MODE0` / RGMII1 mode strap

This still makes sense in managed mode, as long as:
- strap levels are fixed at reset (both strapped LOW in this design), and
- firmware configures these GPIOs to LED function (ALT2) after boot.

### Selected LED outputs
| LED Function | GPIO | TQFP Pin | Strap overlap |
|---|---:|---:|---|
| PHY0_LED0 | GPIO_42 | 33 | No |
| PHY0_LED1 | GPIO_43 | 31 | No |
| PHY1_LED0 | GPIO_40 | 35 | Yes (`SD_MODE1`) |
| PHY1_LED1 | GPIO_41 | 34 | Yes (`XMII_MODE0`) |

### LED wiring details
- `GPIO_42` (pin 33), `GPIO_43` (pin 31):
  - LED anode -> `VDD_IO_E` through 470R
  - LED cathode -> GPIO pin (active-low LED drive)
- `GPIO_40` (pin 35) and `GPIO_41` (pin 34), because they are also straps:
  - Same LED connection as above
  - Plus strap resistor 10k from pin to GND (forces startup strap = 0)

This keeps PHY1 LEDs functional and guarantees the strap levels at reset.

## Reset / Strap Latch Sequence (important)
1. Hold STM32 RMII/SPI pins in high-impedance during LAN9645 reset release.
2. Ensure `SERDES_125M_CLK` is present at `S0_CLKP` (pin 45) before releasing LAN9645 `nRESET`.
3. Release LAN9645 `nRESET` (pin 101); straps are latched on this edge.
4. After boot stabilization (~14 ms per checklist), start SPI register configuration.

## Final Implementation Notes
- Do not use SPI alternate pins on GPIO40..43 in this design, because GPIO40/41 are allocated to PHY1 LEDs + straps.
- In managed mode, final copper speed/duplex behavior is configured through registers over SPI.
- `CUPHY_MODE` is strapped low by policy (unused unmanaged features pulled down). Copper link policy is controlled in managed mode by software.
