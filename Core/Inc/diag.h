#ifndef __DIAG_H
#define __DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum
{
  DIAG_FAULT_NONE = 0,
  DIAG_FAULT_SPI_TIMEOUT,
  DIAG_FAULT_SPI_ERROR,
  DIAG_FAULT_CONFIG_MISMATCH,
  DIAG_FAULT_DRDY_TIMEOUT,
  DIAG_FAULT_USB_BUSY_OVERFLOW,
  DIAG_FAULT_RECOVERY_FAILED,
  DIAG_FAULT_INIT_FAILED,
  DIAG_FAULT_COUNT
} diag_fault_code_t;

typedef enum
{
  DIAG_RESET_REASON_UNKNOWN = 0,
  DIAG_RESET_REASON_POWER_ON,
  DIAG_RESET_REASON_PIN,
  DIAG_RESET_REASON_SOFTWARE,
  DIAG_RESET_REASON_INDEPENDENT_WATCHDOG,
  DIAG_RESET_REASON_WINDOW_WATCHDOG,
  DIAG_RESET_REASON_LOW_POWER,
  DIAG_RESET_REASON_BROWNOUT
} diag_reset_reason_t;

typedef enum
{
  DIAG_RECOVERY_ACTION_NONE = 0,
  DIAG_RECOVERY_ACTION_SPI_RETRY,
  DIAG_RECOVERY_ACTION_ADC_RECONFIGURE,
  DIAG_RECOVERY_ACTION_FAULT_HOLD
} diag_recovery_action_t;

typedef enum
{
  DIAG_RECOVERY_RESULT_NONE = 0,
  DIAG_RECOVERY_RESULT_SUCCESS,
  DIAG_RECOVERY_RESULT_FAILED
} diag_recovery_result_t;

typedef struct
{
  uint32_t total_faults;
  uint32_t fault_count[DIAG_FAULT_COUNT];
  diag_fault_code_t last_fault;
  diag_reset_reason_t reset_reason;
  diag_recovery_action_t last_recovery_action;
  diag_recovery_result_t last_recovery_result;
  uint8_t last_protocol_status;
} diag_snapshot_t;

void diag_init(void);
void diag_record_fault(diag_fault_code_t code, uint8_t protocol_status);
void diag_count_fault(diag_fault_code_t code, uint8_t protocol_status);
void diag_record_recovery(diag_recovery_action_t action, diag_recovery_result_t result);
diag_reset_reason_t diag_get_reset_reason(void);
uint32_t diag_get_fault_count(diag_fault_code_t code);
void diag_get_snapshot(diag_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif

#endif /* __DIAG_H */
