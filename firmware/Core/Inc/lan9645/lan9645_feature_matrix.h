#ifndef LAN9645_FEATURE_MATRIX_H
#define LAN9645_FEATURE_MATRIX_H

/**
 * @file lan9645_feature_matrix.h
 * @brief Linux-reference feature inventory and apply control for STM32 LAN9645 BSP.
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "app_common_types.h"
#include "lan9645/lan9645_core.h"

/**
 * @brief Feature IDs mapped from Linux LAN9645x reference modules.
 */
typedef enum {
  LAN9645_FEATURE_AFI = 0,
  LAN9645_FEATURE_BUM,
  LAN9645_FEATURE_CBS,
  LAN9645_FEATURE_CUT_THRU,
  LAN9645_FEATURE_DCB,
  LAN9645_FEATURE_EEE,
  LAN9645_FEATURE_ETS,
  LAN9645_FEATURE_FP,
  LAN9645_FEATURE_HSR_PRP,
  LAN9645_FEATURE_LAG,
  LAN9645_FEATURE_MAC,
  LAN9645_FEATURE_MANUAL_INJ,
  LAN9645_FEATURE_MDB,
  LAN9645_FEATURE_MIRROR,
  LAN9645_FEATURE_MQPRIO,
  LAN9645_FEATURE_MRP,
  LAN9645_FEATURE_NPI,
  LAN9645_FEATURE_PHYLINK,
  LAN9645_FEATURE_POLICE,
  LAN9645_FEATURE_PSFP,
  LAN9645_FEATURE_PTP,
  LAN9645_FEATURE_QOS,
  LAN9645_FEATURE_STREAM,
  LAN9645_FEATURE_TAPRIO,
  LAN9645_FEATURE_TBF,
  LAN9645_FEATURE_TC_FLOWER,
  LAN9645_FEATURE_TC_MATCHALL,
  LAN9645_FEATURE_VCAP,
  LAN9645_FEATURE_VLAN,
  LAN9645_FEATURE_STATS,
  LAN9645_FEATURE_COUNT
} lan9645_feature_id_t;

/**
 * @brief Runtime apply/result state for one Linux-reference feature.
 */
typedef struct {
  uint8_t enabled;           /**< Set when the feature is enabled in the STM32 control profile. */
  uint8_t register_backed;   /**< Set when register-level implementation exists in this firmware. */
  uint8_t configured;        /**< Set after at least one successful apply operation. */
  uint8_t reserved0;         /**< Reserved for alignment. */
  app_status_t last_status;  /**< Last apply status returned for this feature. */
  uint32_t apply_count;      /**< Number of apply attempts. */
  uint32_t fail_count;       /**< Number of failed apply attempts. */
} lan9645_feature_state_t;

/**
 * @brief Aggregate summary of the Linux-reference feature matrix state.
 */
typedef struct {
  uint32_t total_count;         /**< Total number of tracked features. */
  uint32_t enabled_count;       /**< Number of currently enabled features. */
  uint32_t register_backed_count; /**< Number of features with register-level support. */
  uint32_t configured_count;    /**< Number of features applied successfully at least once. */
  uint32_t failed_count;        /**< Number of features with at least one failed apply. */
} lan9645_feature_summary_t;

/**
 * @brief Matrix context that tracks all Linux-reference feature entries.
 */
typedef struct lan9645_feature_matrix {
  lan9645_core_t *core;                               /**< Core context used for register-backed applies. */
  uint8_t initialized;                                /**< Set when matrix context has been initialized. */
  uint8_t reserved0;
  uint16_t reserved1;
  lan9645_feature_state_t features[LAN9645_FEATURE_COUNT]; /**< Per-feature runtime states. */
} lan9645_feature_matrix_t;

/**
 * @brief Initializes the Linux-reference feature matrix context.
 *
 * @param[out] matrix Destination matrix context.
 * @param[in] core Core context used for register-backed feature apply routines.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_init(lan9645_feature_matrix_t *matrix, lan9645_core_t *core);

/**
 * @brief Returns the canonical feature token for a feature ID.
 *
 * @param[in] feature Feature ID.
 *
 * @return Zero-terminated token or `"unknown"` when out of range.
 */
const char *lan9645_feature_matrix_name(lan9645_feature_id_t feature);

/**
 * @brief Parses a feature token into a feature ID.
 *
 * @param[in] name Zero-terminated feature token.
 * @param[out] out Parsed feature ID.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_parse(const char *name, lan9645_feature_id_t *out);

/**
 * @brief Enables or disables one feature in the runtime profile.
 *
 * @param[in,out] matrix Matrix context.
 * @param[in] feature Feature ID.
 * @param[in] enable Set to 1 to enable, 0 to disable.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_set_enable(
  lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature,
  uint8_t enable
);

/**
 * @brief Returns the runtime state of one feature.
 *
 * @param[in] matrix Matrix context.
 * @param[in] feature Feature ID.
 * @param[out] out Destination state snapshot.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_get_state(
  const lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature,
  lan9645_feature_state_t *out
);

/**
 * @brief Applies one feature configuration according to current profile and support level.
 *
 * @param[in,out] matrix Matrix context.
 * @param[in] feature Feature ID.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_apply(
  lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature
);

/**
 * @brief Applies all currently enabled features.
 *
 * @param[in,out] matrix Matrix context.
 *
 * @return @ref APP_OK when all enabled features apply successfully.
 */
app_status_t lan9645_feature_matrix_apply_all(lan9645_feature_matrix_t *matrix);

/**
 * @brief Builds a summary snapshot of the current matrix status.
 *
 * @param[in] matrix Matrix context.
 * @param[out] out Destination summary.
 *
 * @return @ref APP_OK on success.
 */
app_status_t lan9645_feature_matrix_get_summary(
  const lan9645_feature_matrix_t *matrix,
  lan9645_feature_summary_t *out
);

#ifdef __cplusplus
}
#endif

#endif /* LAN9645_FEATURE_MATRIX_H */
