# STM32 LwIP General Settings - Phase 1 (No Code)

## Scope
This file locks the first `STM32CubeMX + LwIP` general network settings for ping bring-up.
Goal: make CuPHY/SGMII ping tests deterministic before advanced features.

---

## 1) Locked Static Network Profile (Recommended)

Use this as the default for first tests (direct PC-to-board or same isolated switch segment):

| Item | Value |
|---|---|
| STM32 static IP | `192.168.10.50` |
| Subnet mask | `255.255.255.0` |
| Gateway | `0.0.0.0` |
| DNS | `0.0.0.0` (not needed for ping) |

Why this profile:
- simple isolated lab network
- no DHCP dependency
- no router dependency for ICMP ping tests

---

## 2) PC Test Host Settings (for ping validation)

Set your PC Ethernet adapter to:

| Item | Value |
|---|---|
| PC static IP | `192.168.10.100` |
| Subnet mask | `255.255.255.0` |
| Gateway | blank or `0.0.0.0` |

Ping target:
- `ping 192.168.10.50`

---

## 3) CubeMX LwIP General Settings to Track

- `DHCP = Disabled`
- `IPv4 = Enabled`
- `ARP = Enabled`
- `ICMP = Enabled`
- `NO_SYS = 1` (no FreeRTOS for phase 1)
- PHY driver in platform settings = `USER_PHY`

---

## 4) Phase-1 Acceptance Criteria

1. STM32 boots with static IP `192.168.10.50`.
2. PC on same subnet can ping STM32 with low packet loss.
3. No DHCP or mDNS required for first validation.

---

## 5) Later Phase (Not now)

After stable ping across Cu0/Cu1/SFP0/SFP1:
- optional move to DHCP
- optional gateway/DNS configuration for routed networks

