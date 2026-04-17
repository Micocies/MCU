#include "fault_policy.h"

#include <string.h>

#include "app_config.h"

static fault_policy_snapshot_t g_fault_policy;

void fault_policy_init(void)
{
  memset(&g_fault_policy, 0, sizeof(g_fault_policy));
}

fault_policy_decision_t fault_policy_on_fault(diag_fault_code_t code)
{
  fault_policy_decision_t decision;

  decision.action = DIAG_RECOVERY_ACTION_FAULT_HOLD;
  decision.enter_fault_hold = 0U;

  if (code == DIAG_FAULT_USB_BUSY_OVERFLOW)
  {
    decision.action = DIAG_RECOVERY_ACTION_NONE;
    return decision;
  }

  g_fault_policy.consecutive_failures++;

  if (g_fault_policy.consecutive_failures > APP_RECOVERY_HOLD_THRESHOLD)
  {
    decision.enter_fault_hold = 1U;
  }
  else if (code == DIAG_FAULT_SPI_TIMEOUT)
  {
    if (g_fault_policy.recovery_attempts < APP_RECOVERY_SPI_RETRY_LIMIT)
    {
      decision.action = DIAG_RECOVERY_ACTION_SPI_RETRY;
      g_fault_policy.recovery_attempts++;
    }
    else
    {
      decision.enter_fault_hold = 1U;
    }
  }
  else if ((code == DIAG_FAULT_CONFIG_MISMATCH) ||
           (code == DIAG_FAULT_DRDY_TIMEOUT) ||
           (code == DIAG_FAULT_SPI_ERROR))
  {
    if (g_fault_policy.recovery_attempts < APP_RECOVERY_RECONFIGURE_LIMIT)
    {
      decision.action = DIAG_RECOVERY_ACTION_ADC_RECONFIGURE;
      g_fault_policy.recovery_attempts++;
    }
    else
    {
      decision.enter_fault_hold = 1U;
    }
  }
  else
  {
    decision.enter_fault_hold = 1U;
  }

  if (decision.enter_fault_hold != 0U)
  {
    decision.action = DIAG_RECOVERY_ACTION_FAULT_HOLD;
  }

  g_fault_policy.last_action = decision.action;
  return decision;
}

void fault_policy_record_recovery_result(diag_recovery_action_t action, bool success)
{
  g_fault_policy.last_action = action;
  g_fault_policy.last_result = (success != false) ? DIAG_RECOVERY_RESULT_SUCCESS : DIAG_RECOVERY_RESULT_FAILED;

  if (success != false)
  {
    g_fault_policy.consecutive_failures = 0U;
    g_fault_policy.recovery_attempts = 0U;
  }
}

void fault_policy_get_snapshot(fault_policy_snapshot_t *snapshot)
{
  if (snapshot == NULL)
  {
    return;
  }

  *snapshot = g_fault_policy;
}
