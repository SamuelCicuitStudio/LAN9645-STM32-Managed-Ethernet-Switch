# LAN9645 STM32 SPI BSP Porting Guide (Mandatory, Linux-Source-Aligned)

## Objective
Define the exact implementation guide for STM32 LAN9645 BSP work, with the primary goal of full SPI control, using only:
- `D:/Freelancer/luisspranger/Design/firmware/lan9645x`

No guessed SPI/register assumptions are allowed.

## Primary Goal (Locked)
Deliver a STM32 BSP where LAN9645 management is fully controlled through SPI:
- deterministic read/write access to LAN9645 register space
- deterministic IF_CTRL and IF_CFGSTAT control
- deterministic target-map based addressing
- deterministic reset/recovery and re-initialization over SPI

## Scope
This guide is for:
- SPI low-level transport
- register addressing/model
- target base mapping
- LAN9645 management sequencing over SPI from reset to operational state

This guide is not for:
- FRER/VCAP advanced feature bring-up (later phase)
- Linux DSA integration itself

## Source Of Truth (Locked)
Use only these Linux BSP source files as authority:
- `Design/firmware/lan9645x/mfd/lan9645x-spi.c`
- `Design/firmware/lan9645x/mfd/lan9645x.h`
- `Design/firmware/lan9645x/lan9645x_regs.h`
- `Design/firmware/lan9645x/lan9645x_main.c`
- `Design/firmware/lan9645x/lan9645x_main.h`
- `Design/firmware/lan9645x/lan9645x_phylink.c`
- `Design/firmware/lan9645x/lan9645x_vlan.c`
- `Design/firmware/lan9645x/lan9645x_stats.c`

## Complete Sequencing Policy (All lan9645x Files)
No file in `Design/firmware/lan9645x` is ignored. Every file is sequenced by phase.

### Phase 1-10 (Baseline SPI BSP path)
- Mandatory baseline files:
  - `mfd/lan9645x-spi.c`, `mfd/lan9645x.h`, `lan9645x_regs.h`
  - `lan9645x_main.c`, `lan9645x_main.h`
  - `lan9645x_mac.c`, `lan9645x_npi.c`, `lan9645x_phylink.c`, `lan9645x_vlan.c`
  - `lan9645x_stats.c`, `lan9645x_stats.h`
- Baseline diagnostic reference:
  - `lan9645x_manual_inj.c`

### Phase 11 (Advanced L2 offloads)
- `lan9645x_lag.c`
- `lan9645x_mdb.c`
- `lan9645x_mirror.c`
- `lan9645x_bum.c`

### Phase 12 (QoS and TSN scheduling)
- `lan9645x_qos.c`
- `lan9645x_dcb.c`
- `lan9645x_mqprio.c`
- `lan9645x_ets.c`
- `lan9645x_cbs.c`
- `lan9645x_tbf.c`
- `lan9645x_taprio.c`
- `lan9645x_cut_thru.c`
- `lan9645x_afi.c`
- `lan9645x_eee.c`

### Phase 13 (VCAP/TC classification)
- `lan9645x_tc_flower.c`
- `lan9645x_tc_matchall.c`
- `lan9645x_vcap_ag_api.c`
- `lan9645x_vcap_ag_api.h`
- `lan9645x_vcap_impl.c`
- `lan9645x_vcap_utils.c`
- `lan9645x_vcap_utils.h`
- `lan9645x_vcap_debugfs.c`

### Phase 14 (FRER/redundancy/protection)
- `lan9645x_hsr_prp.c`
- `lan9645x_mrp.c`
- `lan9645x_mrp.h`
- `lan9645x_psfp.c`
- `lan9645x_stream.c`
- `lan9645x_fp.c`
- `lan9645x_police.c`

### Phase 15 (PTP/time)
- `lan9645x_ptp.c`
- `lan9645x_ptp_logs.c`

### Phase 16 (Linux-only integration boundary)
- `lan9645x_netlink_fp.c`
- `lan9645x_netlink_fp.h`
- `lan9645x_netlink_frer.c`
- `lan9645x_netlink_frer.h`
- `lan9645x_netlink_frer_impl.c`
- `lan9645x_netlink_qos.c`
- `lan9645x_netlink_qos.h`
- `Kconfig`
- `Makefile`

### Sequencing Rule
- Phase 1-10 must be completed and validated before Phase 11+ implementation starts.
- Phase 11+ files are tracked as deferred features, not discarded.
- Linux-only files are reference-only and not ported directly into STM32 BSP runtime.

## Mandatory End-to-End Sequencing (Execution Order)
| Step | Phase | What Must Be Completed | Main Inputs | Main Outputs/Gates |
| --- | --- | --- | --- | --- |
| 1 | 0 | Lock baseline and source tree | `Design/firmware/lan9645x` | Baseline lock approved |
| 2 | 1 | Extract SPI protocol constants and formulas | `mfd/lan9645x-spi.c`, `mfd/lan9645x.h` | SPI constants frozen |
| 3 | 2 | Implement STM32 SPI transport with Linux parity | `lan9645_spi.*` | Stable read/write + padding behavior |
| 4 | 3 | Implement target base map and address model | `lan9645_target_map.*`, `lan9645_reg.*` | Address resolution validated |
| 5 | 4 | Align register descriptors and masks | `lan9645_regs.*`, `lan9645x_regs.h` | Descriptor traceability complete |
| 6 | 5 | Implement IF_CTRL/IF_CFGSTAT + reset order | `lan9645_core.*` | Post-reset SPI access deterministic |
| 7 | 6 | Integrate USER_PHY bridge path | generated USER PHY + adapter | ETH PHY calls routed and stable |
| 8 | 7 | Apply managed profile sequencing | `lan9645_profile_managed_rmii1.*` | Deterministic managed config apply |
| 9 | 8 | Add CLI/diag coverage for failures and probes | `cli/*`, `diag/*`, `ui/*` | All critical errors visible on serial |
| 10 | 9 | Validate LwIP bring-up and ping | `lwip.c`, `ethernetif.c`, `lwipopts.h` | Repeatable ping success |
| 11 | 10 | Freeze baseline and validation report | all baseline files | BSP baseline freeze tag |
| 12 | 11-16 | Track advanced modules by deferred phases | all remaining `lan9645x` files | Each file classified as defer/reference/next roadmap |

### Sequencing Acceptance Gates (No Skip Allowed)
1. A step is complete only when its output gate is documented.
2. No forward jump is allowed if the previous step has unresolved errors.
3. Every register or behavior added in STM32 code must cite its Linux source file.

## Decision: Patch Folder
Yes, deleting `Design/firmware/patch` is acceptable.
We no longer depend on patch markdown snippets because we now have the direct Linux BSP source tree in `Design/firmware/lan9645x`.

## Mandatory Technical Baseline

### 1) SPI framing and addressing
STM32 transport must follow Linux MFD model:
- register command width: 24 bits
- write flag mask: `0x80`
- register stride: 4 bytes
- register path transform: downshift by 2 (word-address behavior)
- register header byte order: big-endian
- value handling: native host endianness after IF_CTRL setup

### 2) Padding behavior
Use Linux-aligned policy:
- `MAX_PADDING = 15`
- `DEFAULT_PADDING = 15` (safe startup/reset compatibility)
- runtime padding computed from SPI frequency and access-time formula:
  - `padding = ceil((kHz * access_time_us + 1500) / 8000)`
  - `access_time_us = 2`

### 3) IF controller programming
During bring-up, program and validate:
- `GCB_IF_CTRL`
- `GCB_IF_CFGSTAT`

Byte-order control value must follow Linux model from `mfd/lan9645x.h`:
- LE hosts use `LAN9645X_SPI_BYTE_ORDER_LE` (`0x00000000`)
- BE hosts use `LAN9645X_SPI_BYTE_ORDER_BE` (`0x81818181`)

STM32H743 is little-endian, so use LE setting.

### 4) Target base map
Board defaults must be replaced by Linux MFD resource values for used targets.
Minimum required bases for current managed flow:
- `gcb = 0x4000`
- `qs = 0x8000`
- `chip_top = 0x10000`
- `rew = 0x18000`
- `sys = 0x2C000`
- `hsio = 0x30000`
- `dev0..dev8 = 0x38000 + n*0x4000`
- `qsys = 0x100000`
- `afi = 0x120000`
- `ana = 0x140000`

## Mandatory Bring-Up Sequence
1. Init STM32 SPI peripheral (8-bit data).
2. Init LAN SPI context with startup padding=15.
3. Init register context.
4. Apply Linux-aligned target base map.
5. Assert/deassert external LAN reset.
6. Configure IF controller:
   - write `IF_CTRL`
   - compute/write `IF_CFGSTAT`
   - read back and validate
7. Perform LAN soft reset sequence (core + RAM init flow).
8. Re-apply IF controller config if required by reset effect.
9. Apply mandatory SPI-managed LAN configuration profile.
10. Enable operational ports and verify link/status.

## Implementation Rules (No Optional Path)
- Do not keep provisional/guessed base addresses.
- Do not keep provisional/guessed padding limits.
- Do not use mixed documentation sources for constants if Linux source exists.
- Every register constant used in STM32 code must be traceable to `lan9645x_regs.h`.
- Every low-level SPI behavior decision must be traceable to `mfd/lan9645x-spi.c` or `mfd/lan9645x.h`.

## STM32 File Responsibilities (Planned)
- `firmware/Core/Inc/lan9645/lan9645_spi.h`:
  - Linux-aligned SPI constants and config API
- `firmware/Core/Src/lan9645/lan9645_spi.c`:
  - Linux-aligned command framing, padding behavior
- `firmware/Core/Src/lan9645/lan9645_target_map.c`:
  - Linux resource base map (used targets)
- `firmware/Core/Inc|Src/lan9645/lan9645_regs.*`:
  - descriptors/fields verified against `lan9645x_regs.h`
- `firmware/Core/Inc|Src/lan9645/lan9645_core.*`:
  - IF_CTRL/IF_CFGSTAT setup API + reset sequencing

## Validation Checklist (Mandatory Before Ping)
1. SPI read/write stable at configured frequency.
2. IF controller readback matches programmed value.
3. Padding sweep confirms stable operation at target SPI rate.
4. Feature-disable and key status registers are readable and consistent.
5. Port enable/disable register writes are effective.
6. SPI-managed configuration profile applies without timeout/error.
7. Ping path test passes on targeted LAN ports.

## Execution Note
Next step after this guide:
- update `LAN9645_library_implementation_plan.md` to align phase-by-phase tasks with this mandatory Linux-source-aligned BSP flow.
