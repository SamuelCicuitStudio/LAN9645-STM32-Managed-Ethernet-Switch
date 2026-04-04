# Firmware Documentation

This folder contains firmware-side implementation specifications for STM32H743 + LAN9645 managed mode.

## Files
- `LAN9645_managed_control_library_spec.md`: main managed-control library contract.
- `LAN9645_stm32_firmware_direction_hal_lwip.md`: HAL ETH + LwIP architecture direction.
- `LAN9645_stm32_physical_comm_cli_plan.md`: physical interfaces, error IDs, and CLI control model.
- `LAN9645_switch_config_and_ping_bringup.md`: switch setup and ping bring-up sequence.
- `STM32_generated_code_ping_bringup_checklist.md`: audit-based mandatory ON/check list for current generated firmware before ping validation.
- `STM32H743_LwIP_activation_and_PHY_driver_choice.md`: CubeMX activation rules and PHY selection.
- `STM32_LwIP_general_settings_phase1.md`: locked initial LwIP settings.
- `STM32_runtime_io_cli_logic_spec.md`: serial ownership, bridge mode, logging, LED/button runtime behavior.
