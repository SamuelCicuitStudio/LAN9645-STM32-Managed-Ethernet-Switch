#include "lan9645/lan9645_reg.h"

#include <string.h>

#include "main.h"

static app_status_t lan9645_reg_check_ctx(const lan9645_reg_t *ctx)
{
  if (ctx == NULL) {
    return APP_EINVAL;
  }
  if ((ctx->initialized == 0u) || (ctx->spi == NULL)) {
    return APP_ESTATE;
  }
  return APP_OK;
}

static app_status_t lan9645_reg_resolve_target_index(
  lan9645_target_t target,
  uint32_t target_instance,
  uint32_t *index_out
)
{
  uint32_t target_base = (uint32_t)target;
  uint32_t idx = 0u;

  if (index_out == NULL) {
    return APP_EINVAL;
  }
  if (target_base >= (uint32_t)LAN9645_TARGET_COUNT) {
    return APP_EINVAL;
  }

  idx = target_base + target_instance;
  if (idx >= (uint32_t)LAN9645_TARGET_COUNT) {
    return APP_EINVAL;
  }

  *index_out = idx;
  return APP_OK;
}

static app_status_t lan9645_reg_desc_to_addr(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t *abs_addr
)
{
  uint32_t tinst = 0u;
  uint32_t ginst = 0u;
  uint32_t rinst = 0u;
  uint32_t target_idx = 0u;
  uint32_t rel_addr = 0u;
  app_status_t st = APP_OK;

  if ((desc == NULL) || (abs_addr == NULL)) {
    return APP_EINVAL;
  }

  tinst = (uint32_t)desc->target_instance + target_instance;
  ginst = (uint32_t)desc->group_instance + group_instance;
  rinst = (uint32_t)desc->reg_instance + reg_instance;

  if (tinst >= desc->target_count) {
    return APP_EINVAL;
  }
  if (ginst >= desc->group_count) {
    return APP_EINVAL;
  }
  if (rinst >= desc->reg_count) {
    return APP_EINVAL;
  }

  st = lan9645_reg_resolve_target_index(desc->target, tinst, &target_idx);
  if (st != APP_OK) {
    return st;
  }
  if (ctx->target_base_valid[target_idx] == 0u) {
    return APP_ESTATE;
  }

  rel_addr = lan9645_reg_desc_rel_addr(desc, group_instance, reg_instance);
  *abs_addr = ctx->target_base[target_idx] + rel_addr;
  return APP_OK;
}

app_status_t lan9645_reg_init(lan9645_reg_t *ctx, lan9645_spi_t *spi)
{
  if ((ctx == NULL) || (spi == NULL)) {
    return APP_EINVAL;
  }

  memset(ctx, 0, sizeof(*ctx));
  ctx->spi = spi;
  ctx->initialized = 1u;
  return APP_OK;
}

app_status_t lan9645_reg_set_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t base_addr
)
{
  uint32_t target_idx = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_resolve_target_index(target, (uint32_t)target_instance, &target_idx);
  if (st != APP_OK) {
    return st;
  }

  ctx->target_base[target_idx] = base_addr;
  ctx->target_base_valid[target_idx] = 1u;
  return APP_OK;
}

app_status_t lan9645_reg_clear_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance
)
{
  uint32_t target_idx = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_resolve_target_index(target, (uint32_t)target_instance, &target_idx);
  if (st != APP_OK) {
    return st;
  }

  ctx->target_base[target_idx] = 0u;
  ctx->target_base_valid[target_idx] = 0u;
  return APP_OK;
}

app_status_t lan9645_reg_get_target_base(
  lan9645_reg_t *ctx,
  lan9645_target_t target,
  uint8_t target_instance,
  uint32_t *base_addr
)
{
  uint32_t target_idx = 0u;
  app_status_t st = APP_OK;

  if (base_addr == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_resolve_target_index(target, (uint32_t)target_instance, &target_idx);
  if (st != APP_OK) {
    return st;
  }
  if (ctx->target_base_valid[target_idx] == 0u) {
    return APP_ESTATE;
  }

  *base_addr = ctx->target_base[target_idx];
  return APP_OK;
}

app_status_t lan9645_reg_read32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t *value)
{
  app_status_t st = APP_OK;

  if (value == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_spi_read32(ctx->spi, reg_addr, value);
}

app_status_t lan9645_reg_write32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t value)
{
  app_status_t st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_spi_write32(ctx->spi, reg_addr, value);
}

app_status_t lan9645_reg_rmw32(lan9645_reg_t *ctx, uint32_t reg_addr, uint32_t mask, uint32_t value)
{
  uint32_t reg_val = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_read32(ctx, reg_addr, &reg_val);
  if (st != APP_OK) {
    return st;
  }

  reg_val = (reg_val & ~mask) | (value & mask);
  return lan9645_reg_write32(ctx, reg_addr, reg_val);
}

app_status_t lan9645_reg_wait_field(
  lan9645_reg_t *ctx,
  uint32_t reg_addr,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
)
{
  uint32_t start_ms = 0u;
  uint32_t now_ms = 0u;
  uint32_t val = 0u;
  app_status_t st = APP_OK;

  if (timeout_ms == 0u) {
    return APP_EINVAL;
  }

  start_ms = HAL_GetTick();
  now_ms = start_ms;

  while ((now_ms - start_ms) <= timeout_ms) {
    st = lan9645_reg_read32(ctx, reg_addr, &val);
    if (st != APP_OK) {
      return st;
    }

    if ((val & mask) == (expected & mask)) {
      if (last_read != NULL) {
        *last_read = val;
      }
      return APP_OK;
    }

    HAL_Delay(1u);
    now_ms = HAL_GetTick();
  }

  if (last_read != NULL) {
    *last_read = val;
  }

  return APP_ETIMEOUT;
}

app_status_t lan9645_reg_read32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t *value
)
{
  uint32_t abs_addr = 0u;
  app_status_t st = APP_OK;

  if (value == NULL) {
    return APP_EINVAL;
  }

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_desc_to_addr(ctx, desc, target_instance, group_instance, reg_instance, &abs_addr);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_read32(ctx, abs_addr, value);
}

app_status_t lan9645_reg_write32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t value
)
{
  uint32_t abs_addr = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_desc_to_addr(ctx, desc, target_instance, group_instance, reg_instance, &abs_addr);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_write32(ctx, abs_addr, value);
}

app_status_t lan9645_reg_rmw32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t value
)
{
  uint32_t abs_addr = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_desc_to_addr(ctx, desc, target_instance, group_instance, reg_instance, &abs_addr);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_rmw32(ctx, abs_addr, mask, value);
}

app_status_t lan9645_reg_wait32_desc(
  lan9645_reg_t *ctx,
  const lan9645_reg_desc_t *desc,
  uint32_t target_instance,
  uint32_t group_instance,
  uint32_t reg_instance,
  uint32_t mask,
  uint32_t expected,
  uint32_t timeout_ms,
  uint32_t *last_read
)
{
  uint32_t abs_addr = 0u;
  app_status_t st = APP_OK;

  st = lan9645_reg_check_ctx(ctx);
  if (st != APP_OK) {
    return st;
  }

  st = lan9645_reg_desc_to_addr(ctx, desc, target_instance, group_instance, reg_instance, &abs_addr);
  if (st != APP_OK) {
    return st;
  }

  return lan9645_reg_wait_field(ctx, abs_addr, mask, expected, timeout_ms, last_read);
}
