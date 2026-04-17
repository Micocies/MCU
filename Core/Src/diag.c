#include "diag.h"

#include <string.h>

#include "stm32g4xx_hal.h"

static diag_snapshot_t g_diag;

static diag_reset_reason_t diag_read_reset_reason(void)
{
#ifdef UNIT_TEST
  return DIAG_RESET_REASON_UNKNOWN;
#else
  diag_reset_reason_t reason = DIAG_RESET_REASON_UNKNOWN;

  if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != 0U)
  {
    reason = DIAG_RESET_REASON_INDEPENDENT_WATCHDOG;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != 0U)
  {
    reason = DIAG_RESET_REASON_WINDOW_WATCHDOG;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != 0U)
  {
    reason = DIAG_RESET_REASON_SOFTWARE;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != 0U)
  {
    reason = DIAG_RESET_REASON_PIN;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != 0U)
  {
    reason = DIAG_RESET_REASON_LOW_POWER;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) != 0U)
  {
    reason = DIAG_RESET_REASON_BROWNOUT;
  }
  else if (__HAL_RCC_GET_FLAG(RCC_FLAG_OBLRST) == 0U)
  {
    reason = DIAG_RESET_REASON_POWER_ON;
  }

  __HAL_RCC_CLEAR_RESET_FLAGS();
  return reason;
#endif
}

void diag_init(void)
{
  memset(&g_diag, 0, sizeof(g_diag));
  g_diag.reset_reason = diag_read_reset_reason();
}

void diag_record_fault(diag_fault_code_t code, uint8_t protocol_status)
{
  if ((uint32_t)code >= (uint32_t)DIAG_FAULT_COUNT)
  {
    code = DIAG_FAULT_INIT_FAILED;
  }

  g_diag.total_faults++;
  g_diag.fault_count[code]++;
  g_diag.last_fault = code;
  g_diag.last_protocol_status = protocol_status;
}

void diag_record_recovery(diag_recovery_action_t action, diag_recovery_result_t result)
{
  g_diag.last_recovery_action = action;
  g_diag.last_recovery_result = result;
}

diag_reset_reason_t diag_get_reset_reason(void)
{
  return g_diag.reset_reason;
}

uint32_t diag_get_fault_count(diag_fault_code_t code)
{
  if ((uint32_t)code >= (uint32_t)DIAG_FAULT_COUNT)
  {
    return 0U;
  }

  return g_diag.fault_count[code];
}

void diag_get_snapshot(diag_snapshot_t *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }

  *snapshot = g_diag;
}
