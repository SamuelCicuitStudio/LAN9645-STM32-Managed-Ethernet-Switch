# LAN9645 (128-TQFP) LAN Pin + Strap Plan (Precise)

## Locked Project Targets
- Device: LAN9645xS / LAN96459F, 128-pin TQFP
- Copper ports used: CuPHY0 and CuPHY1
- Copper speed target: 10/100 Mbps operation (managed mode configuration over SPI)
- SerDes used: SGMII0 and SGMII1
- External 125 MHz SerDes reference clock used
- Host MCU: STM32H743 (MAC), connected by RGMII
- Management interface: SPI (STM32 SPI host -> LAN9645 SPI client)
- Copper LEDs: enabled (including strap-overlap pins where needed)

## Source Basis
- LAN9645xS Data Sheet DS00006066C
  - Section 3.1.1 (128-pin assignments)
  - Section 3.3.1 (managed-mode GPIO alt functions)
  - Section 3.4 (strap definitions)
- LAN96459F Hardware Design Checklist DS00006273B
  - Section 4 (straps + LED overlap)
  - Section 8 (RGMII)
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

### 125 MHz SerDes reference clock (controlled by STM32)
Use an external low-jitter 125 MHz oscillator and let STM32 control its enable pin.

| Net | From | To | Notes |
|---|---|---|---|
| `SERDES_125M_CLK` | 125 MHz oscillator output | LAN9645 `S0_CLKP` (pin 45) | Clock input for SerDes reference mode |
| `SERDES_125M_EN` | STM32 GPIO output | Oscillator `OE/EN` pin | STM32 controls clock ON/OFF after boot |

Recommended startup behavior:
- Add pull resistor on oscillator enable so clock is ON by default at power-up:
  - If enable pin is active-high (`OE/EN`): 10k pull-up to VCC
  - If enable pin is active-low (`nOE`): 10k pull-down to GND
- Keep clock valid before releasing LAN9645 `nRESET`, because straps are latched at reset release.
- Keep STM32 as control-only for this clock path (enable/disable). Do not source `SERDES_125M_CLK` directly from STM32 MCO.

### RGMII used for STM32H743: RGMII1 (recommended)
Cross as MAC-to-MAC style:
- STM32 `TX*` -> LAN `RGMII1_RX*`
- STM32 `RX*` <- LAN `RGMII1_TX*`

| LAN9645 Signal | GPIO | TQFP Pin | Connect to STM32 ETH signal |
|---|---:|---:|---|
| RGMII1_RXD0 | GPIO_4 | 95 | TXD0 |
| RGMII1_RXD1 | GPIO_5 | 94 | TXD1 |
| RGMII1_RXD2_RXER | GPIO_6 | 93 | TXD2 |
| RGMII1_RXD3 | GPIO_7 | 91 | TXD3 |
| RGMII1_RXC_REFCLK | GPIO_8 | 90 | TX_CLK |
| RGMII1_RXCTL_CRS_DV | GPIO_9 | 89 | TX_CTL/TX_EN |
| RGMII1_TXC_REFCLK | GPIO_10 | 87 | RX_CLK |
| RGMII1_TXCTL_TX_DV | GPIO_11 | 86 | RX_CTL/RX_DV |
| RGMII1_TXD0 | GPIO_12 | 85 | RXD0 |
| RGMII1_TXD1 | GPIO_13 | 83 | RXD1 |
| RGMII1_TXD2 | GPIO_14 | 82 | RXD2 |
| RGMII1_TXD3 | GPIO_15 | 81 | RXD3 |

### SPI management (selected pin group)
Use GPIO[0:3] (keeps GPIO40/41 free for PHY1 LEDs).

| SPI Signal | GPIO | TQFP Pin | Alt |
|---|---:|---:|---:|
| SI_nCS | GPIO_2 | 98 | ALT1 |
| SI_CLK | GPIO_3 | 97 | ALT1 |
| SI_DI (MOSI into LAN9645) | GPIO_0 | 100 | ALT1 |
| SI_DO (MISO out of LAN9645) | GPIO_1 | 99 | ALT1 |

## Exact Startup Strap Plan (Pin -> GND/VCC)

Notes:
- `VCC` means the **matching VDD_IO domain rail** for that GPIO bank.
- If all VDD_IO rails are 3.3V in your design, then every `VCC` below is 3.3V.
- Strap resistors shown are recommended external defaults for this project.

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
| CUPHY_MODE | GPIO_14 / pin 82 | 1 | 47k to VCC (`VDD_IO_B`) |
| SFP_EN | GPIO_15 / pin 81 | 0 | 47k to GND |
| EEPROM_SIZE | GPIO_33 / pin 60 | 0 | 10k to GND (`VDD_IO_D` domain is 3.3V) |
| GPIO_MODE0 | GPIO_36 / pin 41 | 0 | 10k to GND |
| GPIO_MODE1 | GPIO_37 / pin 40 | 0 | 10k to GND |
| SD_MODE0 | GPIO_39 / pin 36 | 0 | 10k to GND |
| SD_MODE1 | GPIO_40 / pin 35 | 0 | 10k to GND |
| XMII_MODE0 | GPIO_41 / pin 34 | 0 | 10k to GND |
| XMII_MODE1 | GPIO_38 / pin 38 | 0 | 10k to GND |

Why 47k on GPIO10..15: these pins are used by RGMII1 data/clock after reset, so weaker straps reduce high-speed loading.

## Copper LED Pin Plan (with strap overlap handled)

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
1. Hold STM32 RGMII/SPI pins in high-impedance during LAN9645 reset release.
2. Ensure `SERDES_125M_CLK` is present at `S0_CLKP` (pin 45) before releasing LAN9645 `nRESET`.
3. Release LAN9645 `nRESET` (pin 101); straps are latched on this edge.
4. After boot stabilization (~14 ms per checklist), start SPI register configuration.

## Final Implementation Notes
- Do not use SPI alternate pins on GPIO40..43 in this design, because GPIO40/41 are allocated to PHY1 LEDs + straps.
- In managed mode, final copper speed/duplex behavior is configured through registers over SPI.
- `CUPHY_MODE=1` above is retained as a deterministic fallback strap choice (10/100 advertise behavior in unmanaged context).
