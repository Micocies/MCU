# STM32G431 采样状态机重构计划

## Summary
- 以“CubeMX 负责硬件初始化，应用层负责状态机”的方式重构，保留 `HAL_Init() / SystemClock_Config() / MX_*_Init()` 入口不变。
- 新应用状态机固定为：`INIT -> BIAS_STABILIZE -> COMM_CHECK -> DARK_CALIBRATE -> WAIT_TRIGGER -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE -> USB_FLUSH`，异常统一进入 `FAULT`。
- 采样时基采用 `TIM6` 周期中断；`TIM6` 和 `DRDY EXTI0` 只置事件标志，不在中断里做 SPI/USB。
- 先实现“通用骨架 + 设备协议适配层”：SPI/GPIO/USB/状态机全部落地，ADC 专有寄存器和命令通过单独协议接口补齐。
- USB 输出采用二进制定长帧；处理链路先做“原始值 -> 线性换算 -> IIR/均值滤波 -> 暗态基线扣除 -> 时间戳 -> 入队发送”。

## Implementation Changes
- CubeMX 配置改动：
  - 使能 `TIM6` 基本定时器和更新中断，生成 `MX_TIM6_Init()`。
  - 使能 `EXTI0_IRQn`，让 `PB0/ADC_DRDY` 真正进入中断。
  - 保持 USB CDC、SPI1、DAC1 现有配置。
  - 将 `ADC_CS` 空闲电平改为高电平，避免上电默认片选有效。
- 应用层结构：
  - 在 `Core` 下新增应用模块，最少拆成 `app`、`adc_protocol`、`usb_stream`、`app_config` 四个子层；`main.c` 只做硬件初始化和 `while(1){ app_run_once(); }`。
  - `app_config` 放全部默认参数：`sample_rate_hz=1000`、`bias_settle_ms=100`、`dark_calibration_samples=256`、`drdy_timeout_ms=10`、`filter_alpha_shift=3`、`usb_queue_depth=16`、`dac_bias_ch1=2048`、`dac_bias_ch2=2048`。
  - `adc_protocol` 负责 `reset/start/read/parse/link_check` 五类动作；第一版给出可编译的通用模板，默认按“START 触发一次转换，DRDY 到来后读固定字节帧”实现，具体 SPI 命令和帧长度通过宏补齐。
- 状态机行为：
  - `INIT`：初始化上下文，拉高 `CS`，复位/释放 ADC，启动 DAC 两路默认偏置，准备 USB 发送队列，不立即开始采样。
  - `BIAS_STABILIZE`：等待 `bias_settle_ms`，期间仅跑 USB 服务。
  - `COMM_CHECK`：发起一次试采样，要求在 `drdy_timeout_ms` 内收到 DRDY 并成功完成 SPI 读帧，否则进 `FAULT`。
  - `DARK_CALIBRATE`：启动 `TIM6`，按正常采样链路收集 `N` 个有效样本，求均值作为暗态基线；校准完成后清零滤波器历史并进入运行态。
  - `WAIT_TRIGGER`：等待 `TIM6` 置位采样事件；到点后发 START 脉冲或协议触发命令，进入 `WAIT_DRDY`。
  - `WAIT_DRDY`：等待 EXTI 事件；超时则计数并进 `FAULT`。
  - `READ_SAMPLE`：在主循环中完成 SPI 读帧和原始码解析。
  - `PROCESS_SAMPLE`：做符号扩展、线性换算默认 1:1、整数 IIR 滤波、基线扣除、微秒级时间戳和序号递增。
  - `USB_FLUSH`：将处理结果写入环形发送队列，尽量按 64 字节 CDC FS 包聚合发送；USB 忙时不阻塞采样，队列满时丢弃最旧帧并置溢出标志。
  - `FAULT`：停止 `TIM6`，拉低 `START`，保留 DAC 偏置，周期性尝试发送故障状态帧，不做自动恢复。
- 公共接口和数据类型：
  - `typedef enum app_state_t { ... } app_state_t;`
  - `void app_init(void);`
  - `void app_run_once(void);`
  - `void app_on_sample_tick_isr(void);`
  - `void app_on_drdy_isr(void);`
  - `bool usb_stream_enqueue(const sample_packet_t *pkt);`
  - `void usb_stream_service(void);`
  - 定义 32 字节小端定长帧 `sample_packet_t`：
    - `magic:u16=0xA55A`
    - `version:u8=1`
    - `state:u8`
    - `flags:u16`
    - `reserved:u16`
    - `sequence:u32`
    - `timestamp_us:u32`
    - `raw_code:i32`
    - `filtered_code:i32`
    - `baseline_code:i32`
    - `corrected_code:i32`
- 中断与 USB 约束：
  - `TIM6_IRQHandler` 只置 `evt_sample_tick=1`。
  - `EXTI0_IRQHandler` 只置 `evt_drdy=1` 并清中断。
  - `CDC_Transmit_FS()` 外包一层发送服务，主循环里调用，不在 ISR 里直接发 USB。
  - 若需要感知发送完成，可在 `CDC_TransmitCplt_FS()` 里只置 `usb_tx_done` 标志。

## Test Plan
- 上电流程：
  - 设备按顺序经过 `INIT/BIAS_STABILIZE/COMM_CHECK/DARK_CALIBRATE`，最终稳定进入 `WAIT_TRIGGER` 循环。
  - DAC 两路在上电后输出默认偏置且在故障态不被误清零。
- 采样链路：
  - `TIM6` 周期触发后产生一次采样请求，DRDY 到来后仅读取一帧，不重复读。
  - 暗态校准恰好累计 `256` 个有效样本，运行态基线值固定不再更新。
  - 处理输出包含原始值、滤波值、基线和扣基线结果，序号连续递增。
- USB 链路：
  - 未枚举或 `USBD_BUSY` 时不阻塞状态机。
  - 队列满后丢弃最旧帧并在后续帧 `flags` 中体现 overflow。
  - 帧长固定 32 字节，两个样本可拼成一个 64 字节 FS 包发送。
- 异常场景：
  - `COMM_CHECK` 无 DRDY 或 SPI 失败进入 `FAULT`。
  - 运行中 DRDY 超时进入 `FAULT`。
  - USB 拔插不影响采样状态机运行，只影响发送队列消费。

## Assumptions
- 当前按“通用外部 SPI ADC”实现，芯片专有初始化命令、读数命令、DRDY 极性、单帧字节数后续在 `adc_protocol` 中补齐。
- 先沿用当前 `PB0` 的上升沿 DRDY 设定；如果真实器件为低有效 DRDY，需要把 EXTI 改为下降沿并同步更新协议配置。
- 第一版不做 DMA、不做自动重试、不做运行时基线慢跟踪、不输出物理单位浮点值；换算默认采用整数 1:1 线性系数。
- 工程继续以 IAR 工程为主，新增源码需同步加入 `EWARM/STM32G431CBT_MCU.ewp`。
