#include "lan9645/lan9645_feature_matrix.h"

/**
 * @file lan9645_feature_matrix.c
 * @brief Linux-reference feature matrix for STM32 LAN9645 control coverage.
 */

#include <ctype.h>
#include <string.h>

#include "diag/diag_core.h"
#include "diag/diag_router.h"
#include "lan9645/lan9645_reg.h"
#include "lan9645/lan9645_regs.h"
#include "lan9645/lan9645_stats.h"
#include "lan9645/lan9645_vlan.h"

#define LAN9645_FEATURE_DEFAULT_VID        (1u)
#define LAN9645_FEATURE_DEFAULT_MEMBER_MASK (0x103u) /* ports 0,1,8 */
#define LAN9645_FEATURE_DEFAULT_UNTAG_MASK  (0x003u) /* untag copper 0/1 */
#define LAN9645_FEATURE_MANUAL_INJ_PORT      (9u)

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
 * @brief Applies MAC baseline verification for managed ports 0, 1, and 8.
 */
static app_status_t lan9645_feature_apply_mac(lan9645_feature_matrix_t *matrix)
{
  static const uint8_t ports[] = {0u, 1u, 8u};
  uint32_t i = 0u;
  app_status_t st = APP_OK;

  for (i = 0u; i < (uint32_t)(sizeof(ports) / sizeof(ports[0])); ++i) {
    lan9645_core_port_cfg_t cfg = {0};
    st = lan9645_core_get_port_cfg(matrix->core, ports[i], &cfg);
    if (st != APP_OK) {
      return st;
    }
  }

  return APP_OK;
}

/**
 * @brief Applies NPI baseline verification by sampling EXT_CPU configuration.
 */
static app_status_t lan9645_feature_apply_npi(lan9645_feature_matrix_t *matrix)
{
  uint32_t ext_cpu_cfg = 0u;
  app_status_t st = lan9645_reg_read32_desc(
    matrix->core->reg,
    &LAN9645_REG_QSYS_EXT_CPU_CFG,
    0u,
    0u,
    0u,
    &ext_cpu_cfg
  );
  if (st != APP_OK) {
    return st;
  }

  if (LAN9645_FIELD_GET(LAN9645_QSYS_EXT_CPU_CFG_EXT_CPU_PORT, ext_cpu_cfg) != 8u) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Applies phylink baseline verification on RGMII1/DEV8 path.
 */
static app_status_t lan9645_feature_apply_phylink(lan9645_feature_matrix_t *matrix)
{
  uint8_t link_up = 0u;
  uint8_t duplex = 0u;
  uint8_t speed = 0u;
  return lan9645_core_get_rgmii_link(matrix->core, 1u, &link_up, &duplex, &speed);
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
 * @brief Enables AFI baseline domain used by Linux reference profile.
 */
static app_status_t lan9645_feature_apply_afi(lan9645_feature_matrix_t *matrix)
{
  uint32_t rd = 0u;
  app_status_t st = lan9645_reg_rmw32_desc(
    matrix->core->reg,
    &LAN9645_REG_AFI_MISC_CTRL,
    0u,
    0u,
    0u,
    LAN9645_AFI_MISC_CTRL_AFI_ENA,
    LAN9645_AFI_MISC_CTRL_AFI_ENA
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    matrix->core->reg,
    &LAN9645_REG_AFI_MISC_CTRL,
    0u,
    0u,
    0u,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & LAN9645_AFI_MISC_CTRL_AFI_ENA) == 0u) {
    return APP_EIO;
  }

  return APP_OK;
}

/**
 * @brief Configures manual-injection header mode for the dedicated CPU injection port.
 */
static app_status_t lan9645_feature_apply_manual_inj(lan9645_feature_matrix_t *matrix)
{
  const uint32_t mask = LAN9645_SYS_PORT_MODE_INCL_INJ_HDR;
  const uint32_t value = LAN9645_SYS_PORT_MODE_INCL_INJ_HDR_SET(1u);
  uint32_t rd = 0u;
  app_status_t st = lan9645_reg_rmw32_desc(
    matrix->core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    LAN9645_FEATURE_MANUAL_INJ_PORT,
    mask,
    value
  );
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_read32_desc(
    matrix->core->reg,
    &LAN9645_REG_SYS_PORT_MODE,
    0u,
    0u,
    LAN9645_FEATURE_MANUAL_INJ_PORT,
    &rd
  );
  if (st != APP_OK) {
    return st;
  }
  if ((rd & mask) != value) {
    return APP_EIO;
  }

  return APP_OK;
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
  matrix->features[LAN9645_FEATURE_MANUAL_INJ].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_NPI].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_PHYLINK].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_VLAN].register_backed = 1u;
  matrix->features[LAN9645_FEATURE_STATS].register_backed = 1u;

  matrix->features[LAN9645_FEATURE_AFI].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MANUAL_INJ].enabled = 1u;
  matrix->features[LAN9645_FEATURE_MAC].enabled = 1u;
  matrix->features[LAN9645_FEATURE_NPI].enabled = 1u;
  matrix->features[LAN9645_FEATURE_PHYLINK].enabled = 1u;
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
      st = lan9645_feature_apply_afi(matrix);
      break;
    case LAN9645_FEATURE_MAC:
      st = lan9645_feature_apply_mac(matrix);
      break;
    case LAN9645_FEATURE_MANUAL_INJ:
      st = lan9645_feature_apply_manual_inj(matrix);
      break;
    case LAN9645_FEATURE_NPI:
      st = lan9645_feature_apply_npi(matrix);
      break;
    case LAN9645_FEATURE_PHYLINK:
      st = lan9645_feature_apply_phylink(matrix);
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
