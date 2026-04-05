#include "lan9645/lan9645_feature_matrix.h"

/**
 * @file lan9645_feature_matrix.c
 * @brief Linux-reference feature matrix for STM32 LAN9645 control coverage.
 */

#include <ctype.h>
#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_afi.h"
#include "lan9645/lan9645_bum.h"
#include "lan9645/lan9645_cbs.h"
#include "lan9645/lan9645_cut_thru.h"
#include "lan9645/lan9645_dcb.h"
#include "lan9645/lan9645_eee.h"
#include "lan9645/lan9645_fp.h"
#include "lan9645/lan9645_hsr_prp.h"
#include "lan9645/lan9645_lag.h"
#include "lan9645/lan9645_mac.h"
#include "lan9645/lan9645_manual_inj.h"
#include "lan9645/lan9645_mdb.h"
#include "lan9645/lan9645_mirror.h"
#include "lan9645/lan9645_mrp.h"
#include "lan9645/lan9645_npi.h"
#include "lan9645/lan9645_phylink.h"
#include "lan9645/lan9645_police.h"
#include "lan9645/lan9645_ptp.h"
#include "lan9645/lan9645_psfp.h"
#include "lan9645/lan9645_qos.h"
#include "lan9645/lan9645_stats.h"
#include "lan9645/lan9645_stream.h"
#include "lan9645/lan9645_tc_flower.h"
#include "lan9645/lan9645_tc_matchall.h"
#include "lan9645/lan9645_tbf.h"
#include "lan9645/lan9645_taprio.h"
#include "lan9645/lan9645_vcap.h"
#include "lan9645/lan9645_vlan.h"

#define LAN9645_FEATURE_DEFAULT_VID        (1u)
#define LAN9645_FEATURE_DEFAULT_MEMBER_MASK (0x103u) /* ports 0,1,8 */
#define LAN9645_FEATURE_DEFAULT_UNTAG_MASK  (0x003u) /* untag copper 0/1 */

static const char *s_feature_names[LAN9645_FEATURE_COUNT] = {
  "afi",
  "bum",
  "cbs",
  "cut_thru",
  "dcb",
  "eee",
  "ets",
  "fp",
  "hsr_prp",
  "lag",
  "mac",
  "manual_inj",
  "mdb",
  "mirror",
  "mqprio",
  "mrp",
  "npi",
  "phylink",
  "police",
  "psfp",
  "ptp",
  "qos",
  "stream",
  "taprio",
  "tbf",
  "tc_flower",
  "tc_matchall",
  "vcap",
  "vlan",
  "stats"
};

/**
 * @brief Case-insensitive token comparison for feature names.
 */
static uint8_t lan9645_feature_name_eq(const char *a, const char *b)
{
  if ((a == NULL) || (b == NULL)) {
    return 0u;
  }

  while ((*a != '\0') && (*b != '\0')) {
    if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
      return 0u;
    }
    ++a;
    ++b;
  }

  return ((*a == '\0') && (*b == '\0')) ? 1u : 0u;
}

/**
 * @brief Reports one feature-apply failure into diagnostics and routed serial output.
 */
static void lan9645_feature_report_fail(
  lan9645_feature_id_t feature,
  diag_severity_t sev,
  app_status_t st,
  uint32_t step
)
{
  diag_event_t evt = {0};

  evt.id = DIAG_EVT_LAN_FEAT_CFG_FAIL;
  evt.severity = sev;
  evt.dev = (uint32_t)feature;
  evt.reg = 0u;
  evt.arg0 = (uint32_t)(int32_t)st;
  evt.arg1 = step;

  (void)diag_core_report(&evt);
  (void)diag_router_write_event(&evt);
}

/**
 * @brief Applies default VLAN baseline used by the managed STM32 profile.
 */
static app_status_t lan9645_feature_apply_vlan(lan9645_feature_matrix_t *matrix)
{
  app_status_t st = lan9645_vlan_init();
  if (st != APP_OK) {
    return st;
  }

  return lan9645_vlan_set(
    matrix->core,
    LAN9645_FEATURE_DEFAULT_VID,
    LAN9645_FEATURE_DEFAULT_MEMBER_MASK,
    LAN9645_FEATURE_DEFAULT_UNTAG_MASK
  );
}

/**
 * @brief Applies counter service baseline initialization.
 */
static app_status_t lan9645_feature_apply_stats(lan9645_feature_matrix_t *matrix)
{
  (void)matrix;
  return lan9645_stats_init();
}

/**
 * @brief Initializes matrix state and marks baseline register-backed features.
 */
app_status_t lan9645_feature_matrix_init(lan9645_feature_matrix_t *matrix, lan9645_core_t *core)
{
  if ((matrix == NULL) || (core == NULL) || (core->initialized == 0u) || (core->reg == NULL)) {
    return APP_EINVAL;
  }

  memset(matrix, 0, sizeof(*matrix));
  matrix->core = core;
  matrix->initialized = 1u;

  for (uint32_t i = 0u; i < (uint32_t)LAN9645_FEATURE_COUNT; ++i) {
    matrix->features[i].last_status = APP_ENOTSUP;
  }

  matrix->features[LAN9645_FEATURE_MAC].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_AFI].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_BUM].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_CBS].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_CUT_THRU].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_DCB].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_EEE].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_ETS].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_FP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_HSR_PRP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_LAG].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_MANUAL_INJ].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_MDB].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_MIRROR].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_MRP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_NPI].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_MQPRIO].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_POLICE].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_PSFP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_PTP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_PHYLINK].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_QOS].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_STREAM].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_TC_MATCHALL].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_TC_FLOWER].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_TBF].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_TAPRIO].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_VCAP].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_VLAN].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_STATS].register_backed = 1u;

  matrix->features[LAN9645_FEATURE_AFI].enabled = 1u;
  matrix->features[LAN9645_FEATURE_BUM].enabled = 1u;
  matrix->features[LAN9645_FEATURE_CBS].enabled = 1u;
  matrix->features[LAN9645_FEATURE_CUT_THRU].enabled = 1u;
  matrix->features[LAN9645_FEATURE_DCB].enabled = 1u;
  matrix->features[LAN9645_FEATURE_EEE].enabled = 1u;
  matrix->features[LAN9645_FEATURE_ETS].enabled = 1u;
  matrix->features[LAN9645_FEATURE_FP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_HSR_PRP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_LAG].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MANUAL_INJ].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MAC].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MDB].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MIRROR].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MQPRIO].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MRP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_NPI].enabled = 1u;
  matrix->features[LAN9645_FEATURE_POLICE].enabled = 1u;
  matrix->features[LAN9645_FEATURE_PSFP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_PTP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_PHYLINK].enabled = 1u;
  matrix->features[LAN9645_FEATURE_QOS].enabled = 1u;
  matrix->features[LAN9645_FEATURE_STREAM].enabled = 1u;
  matrix->features[LAN9645_FEATURE_TC_MATCHALL].enabled = 1u;
  matrix->features[LAN9645_FEATURE_TC_FLOWER].enabled = 1u;
  matrix->features[LAN9645_FEATURE_TBF].enabled = 1u;
  matrix->features[LAN9645_FEATURE_TAPRIO].enabled = 1u;
  matrix->features[LAN9645_FEATURE_VCAP].enabled = 1u;
  matrix->features[LAN9645_FEATURE_VLAN].enabled = 1u;
  matrix->features[LAN9645_FEATURE_STATS].enabled = 1u;

  return APP_OK;
}

/**
 * @brief Returns the canonical feature name for a feature ID.
 */
const char *lan9645_feature_matrix_name(lan9645_feature_id_t feature)
{
  if ((uint32_t)feature >= (uint32_t)LAN9645_FEATURE_COUNT) {
    return "unknown";
  }
  return s_feature_names[(uint32_t)feature];
}

/**
 * @brief Parses a feature name token into a feature ID.
 */
app_status_t lan9645_feature_matrix_parse(const char *name, lan9645_feature_id_t *out)
{
  uint32_t i = 0u;

  if ((name == NULL) || (out == NULL) || (*name == '\0')) {
    return APP_EINVAL;
  }

  for (i = 0u; i < (uint32_t)LAN9645_FEATURE_COUNT; ++i) {
    if (lan9645_feature_name_eq(name, s_feature_names[i]) != 0u) {
      *out = (lan9645_feature_id_t)i;
      return APP_OK;
    }
  }

  return APP_ENOTSUP;
}

/**
 * @brief Enables or disables one feature in the runtime profile.
 */
app_status_t lan9645_feature_matrix_set_enable(
  lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature,
  uint8_t enable
)
{
  if ((matrix == NULL) || (matrix->initialized == 0u)) {
    return APP_ESTATE;
  }
  if ((uint32_t)feature >= (uint32_t)LAN9645_FEATURE_COUNT) {
    return APP_EINVAL;
  }

  matrix->features[(uint32_t)feature].enabled = (enable != 0u) ? 1u : 0u;
  return APP_OK;
}

/**
 * @brief Returns one feature state snapshot.
 */
app_status_t lan9645_feature_matrix_get_state(
  const lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature,
  lan9645_feature_state_t *out
)
{
  if ((matrix == NULL) || (matrix->initialized == 0u) || (out == NULL)) {
    return APP_EINVAL;
  }
  if ((uint32_t)feature >= (uint32_t)LAN9645_FEATURE_COUNT) {
    return APP_EINVAL;
  }

  *out = matrix->features[(uint32_t)feature];
  return APP_OK;
}

/**
 * @brief Applies one enabled feature according to implementation coverage.
 */
app_status_t lan9645_feature_matrix_apply(
  lan9645_feature_matrix_t *matrix,
  lan9645_feature_id_t feature
)
{
  lan9645_feature_state_t *state = NULL;
  app_status_t st = APP_OK;

  if ((matrix == NULL) || (matrix->initialized == 0u) || (matrix->core == NULL)) {
    return APP_ESTATE;
  }
  if ((uint32_t)feature >= (uint32_t)LAN9645_FEATURE_COUNT) {
    return APP_EINVAL;
  }

  state = &matrix->features[(uint32_t)feature];
  state->apply_count++;

  if (state->enabled == 0u) {
    state->last_status = APP_ESTATE;
    state->fail_count++;
    return APP_ESTATE;
  }

  if (state->register_backed == 0u) {
    state->last_status = APP_ENOTSUP;
    state->fail_count++;
    lan9645_feature_report_fail(feature, DIAG_SEV_WARN, APP_ENOTSUP, 1u);
    return APP_ENOTSUP;
  }

  switch (feature) {
    case LAN9645_FEATURE_AFI:
      st = lan9645_afi_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_BUM:
      st = lan9645_bum_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_CBS:
      st = lan9645_cbs_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_CUT_THRU:
      st = lan9645_cut_thru_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_DCB:
      st = lan9645_dcb_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_EEE:
      st = lan9645_eee_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_ETS:
      st = lan9645_qos_apply_ets_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_FP:
      st = lan9645_fp_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_HSR_PRP:
      st = lan9645_hsr_prp_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_LAG:
      st = lan9645_lag_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MAC:
      st = lan9645_mac_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MANUAL_INJ:
      st = lan9645_manual_inj_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MDB:
      st = lan9645_mdb_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MIRROR:
      st = lan9645_mirror_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MRP:
      st = lan9645_mrp_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_MQPRIO:
      st = lan9645_qos_apply_mqprio_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_NPI:
      st = lan9645_npi_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_POLICE:
      st = lan9645_police_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_PSFP:
      st = lan9645_psfp_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_PTP:
      st = lan9645_ptp_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_STREAM:
      st = lan9645_stream_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_TC_FLOWER:
      st = lan9645_tc_flower_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_TC_MATCHALL:
      st = lan9645_tc_matchall_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_TBF:
      st = lan9645_tbf_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_TAPRIO:
      st = lan9645_taprio_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_VCAP:
      st = lan9645_vcap_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_PHYLINK:
      st = lan9645_phylink_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_QOS:
      st = lan9645_qos_apply_baseline(matrix->core);
      break;
    case LAN9645_FEATURE_VLAN:
      st = lan9645_feature_apply_vlan(matrix);
      break;
    case LAN9645_FEATURE_STATS:
      st = lan9645_feature_apply_stats(matrix);
      break;
    default:
      st = APP_ENOTSUP;
      break;
  }

  state->last_status = st;
  if (st == APP_OK) {
    state->configured = 1u;
  } else {
    state->fail_count++;
    lan9645_feature_report_fail(feature, DIAG_SEV_ERR, st, 2u);
  }

  return st;
}

/**
 * @brief Applies all enabled features and preserves deterministic failure statistics.
 */
app_status_t lan9645_feature_matrix_apply_all(lan9645_feature_matrix_t *matrix)
{
  app_status_t first_fail = APP_OK;

  if ((matrix == NULL) || (matrix->initialized == 0u)) {
    return APP_ESTATE;
  }

  for (uint32_t i = 0u; i < (uint32_t)LAN9645_FEATURE_COUNT; ++i) {
    app_status_t st = APP_OK;
    if (matrix->features[i].enabled == 0u) {
      continue;
    }

    st = lan9645_feature_matrix_apply(matrix, (lan9645_feature_id_t)i);
    if ((st != APP_OK) && (first_fail == APP_OK)) {
      first_fail = st;
    }
  }

  return first_fail;
}

/**
 * @brief Computes a matrix summary across all Linux-reference feature states.
 */
app_status_t lan9645_feature_matrix_get_summary(
  const lan9645_feature_matrix_t *matrix,
  lan9645_feature_summary_t *out
)
{
  if ((matrix == NULL) || (matrix->initialized == 0u) || (out == NULL)) {
    return APP_EINVAL;
  }

  memset(out, 0, sizeof(*out));
  out->total_count = (uint32_t)LAN9645_FEATURE_COUNT;

  for (uint32_t i = 0u; i < (uint32_t)LAN9645_FEATURE_COUNT; ++i) {
    const lan9645_feature_state_t *state = &matrix->features[i];
    if (state->enabled != 0u) {
      out->enabled_count++;
    }
    if (state->register_backed != 0u) {
      out->register_backed_count++;
    }
    if (state->configured != 0u) {
      out->configured_count++;
    }
    if (state->fail_count != 0u) {
      out->failed_count++;
    }
  }

  return APP_OK;
}
