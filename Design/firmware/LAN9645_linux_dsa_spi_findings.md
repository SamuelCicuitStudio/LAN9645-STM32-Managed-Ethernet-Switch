# LAN9645 Linux DSA and SPI Findings (Reference for STM32 Port)

## Purpose
This document records what we learned from Linux LAN9645x and related Microchip SPI material, and how it must guide our STM32H743 implementation.

## What We Confirmed

### 1) Managed mode architecture is valid
- LAN9645x can be fully managed externally through:
  - `SPI` for register/configuration access
  - host MAC link (`RGMII` in EVB examples) for frame injection/extraction
  - `IRQ` line for events
- This matches our project architecture concept (external CPU managing LAN9645).

### 2) Linux DSA is a strong reference source
- Linux uses a LAN9645x DSA driver and regmap model for register-level control.
- This is not directly runnable on STM32 bare-metal, but it is a reliable reference for:
  - register naming and field usage
  - ordering of initialization sequences
  - error handling and polling patterns

### 3) Important SPI clue from LAN9645 logs
- Boot log example reports:
  - SPI frequency: `35 MHz`
  - required padding bytes: `9`
- This implies:
  - padding is frequency dependent
  - fixed small padding limits can fail at high SPI clock

### 4) Additional SPI wire-format detail from LAN9662 material
- The transaction uses a 24-bit command/address header:
  - `1 bit` R/W flag (`1 = write`)
  - `23 bits` address path
- Header byte order is big-endian on the wire.
- Write format:
  - `3-byte header` + `4-byte data`
- Read format:
  - `3-byte header` + `N padding bytes` + clock-out bytes for response data
- The data endianness for the 32-bit payload is controlled by interface control configuration (`if_ctrl` endianness setting in the referenced material).
- Practical behavior noted in the material:
  - around/above ~`800 kHz`, at least one read padding byte is required
  - at higher frequencies, larger padding can be required

### 5) Addressing note (must be validated)
- The material states that some register models are word-addressed (32-bit unit), requiring a `<< 2` conversion when building byte-path addressing.
- The same material also shows examples where a word-style argument appears directly in the 23-bit path field.
- For our firmware: do not hard-lock one conversion rule until verified on target hardware with known-good register probes.

### 6) LAN9668 host DTS findings (very useful for target map)
- In `lan9668_ung8385_lan9645x_evb_0_at_lan966x`, the LAN9645 is instantiated as:
  - `compatible = "microchip,lan9645x-spi"`
  - `spi-max-frequency = <35000000>`
- The switch node declares the target resource names explicitly (critical for our `lan9645_target_map` model):
  - `org`, `gcb`, `qs`, `ptp`, `chip_top`, `tas`, `rew`, `vcap`, `vcap1`, `vcap2`, `mep`, `sys`, `hsio`, `dev`, `dev1` ... `dev8`, `uvov`, `qsys`, `afi`, `ana`, `irom`, `iram`, `erom_i2c`, `erom_spi`, `cuphy`, `uart`, `i2c`, `i2c1`, `timers`, `cpu`, `otp`, `wdt`.
- CPU conduit topology in this Linux reference:
  - host-to-switch conduit uses `lan7` as `rgmii-id` with fixed-link `1 Gbps full-duplex`.
  - this is Linux EVB-specific and differs from our STM32 RMII 10/100 target.
- Internal copper PHY addressing in the reference:
  - `cuphy0..cuphy4` at MDIO addresses `0x1..0x5` on `miim1`.
- Interrupt chain in the reference:
  - LAN9645 OIC (`cpu_intr`) routed to host GPIO external IRQ line, active low level in this DTS setup.

## Immediate Impact on Our Firmware

### A) Transport layer constraints
- SPI driver must support dynamic/validated read padding.
- If cap is below needed value at target SPI speed, increase cap and/or reduce SPI clock.
- Keep 24-bit big-endian header handling strict and deterministic.
- Keep a practical profile for first hardware bring-up:
  - start at conservative SPI speed
  - validate stable reads
  - then scale frequency and padding carefully

### B) Register map strategy
- Continue with symbolic register model already started.
- Keep target/base mapping marked as **provisional** until verified against canonical Linux register macro source and datasheet.

### C) Bring-up approach
- Keep staged bring-up:
  1. SPI read/write sanity
  2. chip identity/status sanity
  3. minimal switch init profile
  4. link and port state checks
  5. ping validation through configured path

## What We Can Reuse from Linux Safely
- High-confidence reusable concepts:
  - command/poll completion patterns (MAC table, VLAN table, stats view)
  - module grouping (`ANA`, `QSYS`, `SYS`, `DEV`, `HSIO`)
  - separation of transport layer from switch logic layer
- Not directly reusable as-is:
  - Linux DSA framework calls and netdevice integration
  - parent MFD/regmap plumbing

## Risks We Must Track
- `SPI padding mismatch` at selected STM32 SPI clock.
- `Address unit mismatch` (word-vs-byte path interpretation).
- `Target/base register mapping` mismatch if inferred addresses are incomplete.
- `Interface-mode mismatch` between Linux EVB examples (often RGMII) and our STM32 design intent (RMII host MAC path).

## Project Decision (Locked)
- Use Linux LAN9645x/related material as a **reference baseline**, not direct portable code.
- Implement STM32-native layers:
  - SPI transport
  - register access abstraction
  - LAN9645 core/profile logic
  - diagnostics + CLI over USB CDC / UART4

## Validation Checklist (Before Final Ping Test)
1. SPI transaction integrity verified at chosen SPI clock.
2. Padding requirement characterized on hardware and fixed in runtime config.
3. Address conversion rule (direct path vs shifted path) validated and documented.
4. Essential register reads/writes confirmed stable and repeatable.
5. Port/link status matches physical cable state.
6. IRQ behavior verified (assert, service, clear).
7. Ping test executed with expected forwarding behavior.

## Action Items
1. Update SPI transport limits/settings to tolerate observed high-padding scenarios.
2. Cross-check current target map against canonical Linux register macro source + datasheet.
3. Keep all assumed register mappings explicitly tagged in docs/code until validated.
4. Add CLI-assisted SPI probe flow:
   - run known register reads at multiple SPI frequencies
   - sweep padding values
   - lock the stable frequency/padding pair used for bring-up
