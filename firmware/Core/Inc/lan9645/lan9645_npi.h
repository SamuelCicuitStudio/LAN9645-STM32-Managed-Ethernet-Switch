#ifndef LAN9645_NPI_H
#define LAN9645_NPI_H

/**
 * @file lan9645_npi.h
 * @brief NPI-domain helpers for LAN9645 managed mode.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/** @brief Maximum external CPU queue mask value. */
#define LAN9645_NPI_CPUQ_MASK_MAX     (0xFFu)
/** @brief Maximum IFH prefix selector value. */
#define LAN9645_NPI_IFH_PREFIX_MAX    (3u)
/** @brief Maximum REW tag configuration selector value. */
#define LAN9645_NPI_REW_TAG_CFG_MAX   (3u)
/** @brief Maximum PGID destination mask value. */
#define LAN9645_NPI_PGID_MASK_MAX     (0x3FFu)
/** @brief Maximum supported NPI port index for this board profile. */
#define LAN9645_NPI_PORT_MAX          (9u)

/**
 * @brief One NPI runtime configuration snapshot.
 */
typedef struct {
  uint8_t npi_port;          /**< External CPU/NPI port index. */
  uint8_t ext_cpuq_mask;     /**< `QSYS_EXT_CPU_CFG.EXT_CPUQ_MSK` */
  uint8_t ext_cpu_kill;      /**< `QSYS_EXT_CPU_CFG.EXT_CPU_KILL_ENA` */
  uint8_t int_cpu_kill;      /**< `QSYS_EXT_CPU_CFG.INT_CPU_KILL_ENA` */
  uint8_t incl_xtr_hdr;      /**< `SYS_PORT_MODE.INCL_XTR_HDR` */
  uint8_t incl_inj_hdr;      /**< `SYS_PORT_MODE.INCL_INJ_HDR` */
  uint8_t rew_tag_cfg;       /**< `REW_TAG_CFG.TAG_CFG` */
  uint16_t src_pgid_mask;    /**< `ANA_PGID(PGID_SRC+npi_port).PGID` */
} lan9645_npi_cfg_t;

/**
 * @brief Initializes NPI baseline service state.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_npi_init(void);

/**
 * @brief Reads the current NPI runtime configuration snapshot.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[out] out Destination NPI configuration snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_npi_get_cfg(lan9645_core_t *core, lan9645_npi_cfg_t *out);

/**
 * @brief Writes NPI runtime configuration with readback verification.
 *
 * @param[in,out] core Initialized LAN9645 core context.
 * @param[in] cfg Source NPI configuration.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_npi_set_cfg(lan9645_core_t *core, const lan9645_npi_cfg_t *cfg);

/**
 * @brief Applies deterministic NPI baseline for the CPU-facing conduit port.
 *
 * Baseline policy:
 * - route CPU extraction queues to external CPU port 8
 * - enable CPU-kill protections for extracted/injected traffic loops
 * - set IFH extraction/injection header policy on port 8
 * - disable REW tagging on NPI port
 * - clear reflected forwarding from `PGID_SRC + CPU_PORT`
 *
 * @param[in,out] core Initialized LAN9645 core context.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_npi_apply_baseline(lan9645_core_t *core);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_NPI_H */
