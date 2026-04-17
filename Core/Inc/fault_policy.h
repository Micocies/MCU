#ifndef __FAULT_POLICY_H
#define __FAULT_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "diag.h"

typedef struct
{
  diag_recovery_action_t action;
  uint8_t enter_fault_hold;
} fault_policy_decision_t;

typedef struct
{
  uint32_t consecutive_failures;
  uint32_t recovery_attempts;
  diag_recovery_action_t last_action;
  diag_recovery_result_t last_result;
} fault_policy_snapshot_t;

void fault_policy_init(void);
fault_policy_decision_t fault_policy_on_fault(diag_fault_code_t code);
void fault_policy_record_recovery_result(diag_recovery_action_t action, bool success);
void fault_policy_get_snapshot(fault_policy_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __FAULT_POLICY_H */
