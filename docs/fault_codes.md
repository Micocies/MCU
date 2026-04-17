# V0.2 Fault Code Table

固件版本：`v0.2.0`

诊断故障码定义在 `Core/Inc/diag.h`，USB 帧标志位定义在 `Core/Inc/app_config.h`。故障保持态的周期性故障帧使用固定 32 字节 `sample_packet_t`，其诊断负载含义如下：

- `raw_code`: 最近一次 `diag_fault_code_t`
- `filtered_code`: 该故障码累计次数
- `baseline_code`: `[31:16] reset_reason, [15:8] last_recovery_action, [7:0] last_recovery_result`
- `corrected_code`: `[31:16] consecutive_failures, [15:0] recovery_attempts`

| Code | Name | USB flag | Meaning | Recovery policy |
| --- | --- | --- | --- | --- |
| 0 | `DIAG_FAULT_NONE` | none | 无故障 | 无动作 |
| 1 | `DIAG_FAULT_SPI_TIMEOUT` | `SAMPLE_FLAG_SPI_TIMEOUT` + `SAMPLE_FLAG_SPI_ERROR` | SPI 事务超时 | 最多 3 次重新发起转换 |
| 2 | `DIAG_FAULT_SPI_ERROR` | `SAMPLE_FLAG_SPI_ERROR` | SPI 非超时错误 | `reset + configure + link_check` |
| 3 | `DIAG_FAULT_CONFIG_MISMATCH` | `SAMPLE_FLAG_CONFIG_MISMATCH` | ADS1220 配置寄存器回读与镜像不一致 | `reset + configure + link_check` |
| 4 | `DIAG_FAULT_DRDY_TIMEOUT` | `SAMPLE_FLAG_DRDY_TIMEOUT` | 转换后等待 DRDY 超时 | `reset + configure + link_check` |
| 5 | `DIAG_FAULT_USB_BUSY_OVERFLOW` | `SAMPLE_FLAG_USB_OVERFLOW` | USB 忙导致队列堆积并丢弃最旧帧 | 不中断采样，仅计数并标记新帧 |
| 6 | `DIAG_FAULT_RECOVERY_FAILED` | `SAMPLE_FLAG_RECOVERY_FAILED` | 恢复动作连续失败 | 进入故障保持态 |
| 7 | `DIAG_FAULT_INIT_FAILED` | `SAMPLE_FLAG_FAULT_STATE` | 初始化阶段不可恢复错误，如 DAC 启动失败 | 进入故障保持态 |

复位原因定义：

| Code | Name |
| --- | --- |
| 0 | `DIAG_RESET_REASON_UNKNOWN` |
| 1 | `DIAG_RESET_REASON_POWER_ON` |
| 2 | `DIAG_RESET_REASON_PIN` |
| 3 | `DIAG_RESET_REASON_SOFTWARE` |
| 4 | `DIAG_RESET_REASON_INDEPENDENT_WATCHDOG` |
| 5 | `DIAG_RESET_REASON_WINDOW_WATCHDOG` |
| 6 | `DIAG_RESET_REASON_LOW_POWER` |
| 7 | `DIAG_RESET_REASON_BROWNOUT` |
