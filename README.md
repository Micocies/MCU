# STM32G431CBT_MCU

基于 `STM32G431CBT`、`ADS1220` 和 `USB CDC` 的采样工程。

当前工程版本：`v1.0.0`。

V1.0 在保留现有单 ADS1220 采样/恢复状态机的基础上，把主 USB 数据流切换为固定 `100 pixels` 整帧输出：

`MCU -> USB -> Host -> 10x10 图像显示`

当前硬件链路仍只有单路真实采样值，默认 `FRAME_TYPE_PARTIAL_REAL` 帧中 `pixel 0` 承载真实校正值，其余 99 个像素承载确定性测试图案，保证上位机从第一版开始按固定 10x10 帧协议解析。

当前工程采用“`CubeMX` 负责底层外设初始化，应用层负责状态机调度”的分层方式。系统主流程如下：

`初始化 -> 偏置稳定 -> 通信自检启动 continuous ADC -> DRDY 读数 -> 暗态校准 -> DRDY 连续读样 -> TIM6 输出许可 -> USB 发送`

ADS1220 默认工作在 `turbo + 2000 SPS + continuous conversion`。`START/SYNC` 只在初始化或恢复后启动连续转换链路，运行态由 `DRDY` 驱动读最新 ADC 样本；`TIM6` 只保留为 `1 kHz` 输出许可/降采样节拍，不再负责启动 ADC 转换。新的时序说明见 `docs/continuous_sampling.md`。

V0.2 在 V0.1 的锁定式故障态基础上增加了细分故障码、故障计数、复位原因记录和自动恢复策略。恢复类故障会先进入 `APP_STATE_RECOVER`，连续恢复失败后才进入 `APP_STATE_FAULT` 故障保持态。

## 目录结构

- `Core/Src/main.c`
  CubeMX 生成的主入口，负责时钟、GPIO、SPI、DAC、TIM6、USB 初始化，并在 `while(1)` 中调用 `app_run_once()`
- `Core/Src/app.c`
  应用状态机核心，负责系统启动流程、采样调度、暗态校准、滤波、基线扣除、时间戳和数据打包
- `Core/Src/adc_protocol.c`
  ADS1220 驱动层，负责 `RESET / START/SYNC / POWERDOWN / WREG / RREG / 读 3 字节 / 补码解析`
- `Core/Src/usb_stream.c`
  USB CDC 发送层，负责 420 字节图像帧队列和 32 字节辅助诊断队列管理
- `Core/Src/frame_protocol.c`
  V1.0 固定 10x10 整帧协议、CRC 和基础校验
- `Core/Src/frame_builder.c`
  V1.0 测试帧、半真实帧和占位帧构造
- `Core/Src/diag.c`
  诊断层，负责细分故障码计数、最近故障、最近恢复动作结果和复位原因记录
- `Core/Src/fault_policy.c`
  故障策略层，负责决定重试、重配或进入故障保持态
- `Core/Src/stm32g4xx_it.c`
  中断入口，`EXTI0` 只置 `DRDY` 事件，`TIM6_DAC` 只置采样节拍事件，`USB_LP` 交给 Cube USB 设备栈
- `USB_Device/App/usbd_cdc_if.c`
  USB CDC 适配层，当前主要负责把应用层缓冲区发送到上位机

## 状态机说明

应用层状态定义位于 `Core/Inc/app.h`：

- `APP_STATE_INIT`
  复位 ADS1220，写默认寄存器，启动 DAC 默认偏置
- `APP_STATE_BIAS_STABILIZE`
  等待模拟前端与偏置稳定
- `APP_STATE_COMM_CHECK`
  发起一次试采样，并通过寄存器回读确认 ADS1220 通信正常
- `APP_STATE_DARK_CALIBRATE`
  采集固定数量样本，建立暗态基线
- `APP_STATE_WAIT_TRIGGER`
  等待 TIM6 周期节拍
- `APP_STATE_WAIT_DRDY`
  等待 ADS1220 `DRDY` 下降沿
- `APP_STATE_READ_SAMPLE`
  读取 ADS1220 24 位原始结果
- `APP_STATE_PROCESS_SAMPLE`
  执行滤波、基线扣除、序号和时间戳更新
- `APP_STATE_USB_FLUSH`
  打包成固定 10x10 图像帧并压入 USB 队列
- `APP_STATE_RECOVER`
  按细分故障码执行自动恢复动作，例如 SPI 重试或 ADC 复位重配
- `APP_STATE_FAULT`
  停止采样，周期性发送故障状态帧；只有连续恢复失败或不可恢复初始化错误才进入

## 状态转换流程

下面这一节描述的不是“状态定义”，而是 `Core/Src/app.c` 中真实执行的状态跳转关系。

### 1. 上电启动阶段

启动后，`app_init()` 先把上下文清零、初始化 USB 发送队列、绑定 ADS1220 驱动，并把状态设置为 `APP_STATE_INIT`。

随后主循环进入以下启动链路：

`APP_STATE_INIT -> APP_STATE_BIAS_STABILIZE -> APP_STATE_COMM_CHECK`

各状态的进入和退出条件如下：

- `APP_STATE_INIT`
  - 进入条件：`app_init()` 完成后作为初始状态进入
  - 执行动作：
    - 停止 `TIM6`
    - 拉高 `ADC_CS`
    - 复位 ADS1220
    - 写入 ADS1220 默认寄存器
    - 启动 DAC 两路偏置并写入默认偏置值
  - 正常退出：
    - 全部初始化成功后进入 `APP_STATE_BIAS_STABILIZE`
  - 异常退出：
    - ADC 配置失败先进入 `APP_STATE_RECOVER`，连续恢复失败或 DAC 启动失败进入 `APP_STATE_FAULT`

- `APP_STATE_BIAS_STABILIZE`
  - 进入条件：`APP_STATE_INIT` 成功完成
  - 执行动作：
    - 不做采样，只等待模拟前端和 DAC 偏置稳定
  - 正常退出：
    - 等待时间达到 `APP_BIAS_STABILIZE_MS` 后进入 `APP_STATE_COMM_CHECK`
  - 异常退出：
    - 本状态本身没有主动错误分支

- `APP_STATE_COMM_CHECK`
  - 进入条件：偏置稳定等待完成
  - 执行动作：
    - 调用 `adc_protocol_start_continuous()` 启动 ADS1220 continuous conversion 链路
    - 切换到 `APP_STATE_WAIT_DRDY`
  - 作用：
    - 用和正常采样相同的链路验证 `START once -> DRDY -> SPI 读数 -> 寄存器回读校验` 是否完整可用

### 2. 通信自检阶段

通信自检不是一个单独的“读完就结束”的状态，而是借用正常采样链路跑一遍：

`APP_STATE_COMM_CHECK -> APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE`

此时 `pipeline_mode = APP_PIPELINE_COMM_CHECK`，因此 `APP_STATE_PROCESS_SAMPLE` 会走“自检分支”。

- `APP_STATE_WAIT_DRDY`
  - 进入条件：
    - 来自 `APP_STATE_COMM_CHECK`
    - 或来自校准/运行阶段的 continuous 采样闭环
  - 执行动作：
    - 等待 `DRDY` 中断累计 `pending_drdy_count`
  - 正常退出：
    - 收到一个或多个 `DRDY` 后进入 `APP_STATE_READ_SAMPLE`
  - 异常退出：
    - continuous 已启动后，若超出 `APP_DRDY_TIMEOUT_MS` 仍未等到 `DRDY`，进入 `APP_STATE_RECOVER`
    - 如果当前是通信自检模式，还会额外打上 `SAMPLE_FLAG_COMM_CHECK_FAILED`

- `APP_STATE_READ_SAMPLE`
  - 进入条件：`DRDY` 已到来
  - 执行动作：
    - 在主循环中调用 `adc_protocol_read_sample()` 读取 ADS1220 原始 24 位结果
  - 正常退出：
    - SPI 读数成功后进入 `APP_STATE_PROCESS_SAMPLE`
  - 异常退出：
    - SPI 读数失败则进入 `APP_STATE_RECOVER`
    - 如果当前是通信自检模式，还会额外打上 `SAMPLE_FLAG_COMM_CHECK_FAILED`

- `APP_STATE_PROCESS_SAMPLE`
  - 进入条件：已经得到一帧原始码值
  - 执行动作：
    - 先统一执行一次 `app_filter_raw_code()`
    - 然后根据 `pipeline_mode` 决定后续跳转
  - 自检分支：
    - 调用 `adc_protocol_link_check()` 回读 ADS1220 配置寄存器
    - 如果回读结果和期望寄存器镜像一致，说明通信链路正常，进入 `APP_STATE_DARK_CALIBRATE`
    - 如果回读失败或不一致，进入 `APP_STATE_RECOVER`

因此，自检阶段的结论很明确：

- 自检成功：
  `APP_STATE_PROCESS_SAMPLE -> APP_STATE_DARK_CALIBRATE`
- 自检失败：
  `APP_STATE_PROCESS_SAMPLE -> APP_STATE_RECOVER -> APP_STATE_BIAS_STABILIZE / APP_STATE_FAULT`

### 3. 暗态校准阶段

暗态校准由 `APP_STATE_DARK_CALIBRATE` 启动，但真正的样本累计来自 continuous conversion 下的 `WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE` 循环。

主链路如下：

`APP_STATE_DARK_CALIBRATE -> APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE -> APP_STATE_WAIT_DRDY`

- `APP_STATE_DARK_CALIBRATE`
  - 进入条件：通信自检成功
  - 执行动作：
    - 设置 `pipeline_mode = APP_PIPELINE_CALIBRATION`
    - 清空校准累加器、样本计数、滤波器历史和基线值
    - 启动 `TIM6` 作为 1 kHz 输出许可节拍
  - 正常退出：
    - 进入 `APP_STATE_WAIT_DRDY`

- `APP_STATE_WAIT_TRIGGER`
  - 进入条件：
    - 运行阶段处理完一个 DRDY 样本后
  - 执行动作：
    - 检查是否已有降采样后的最新样本，以及 `TIM6` 是否累计了输出许可
  - 正常退出：
    - 样本和输出许可同时满足则进入 `APP_STATE_USB_FLUSH`
    - 否则回到 `APP_STATE_WAIT_DRDY`，继续保持 ADC 原始样本链路前进

- `APP_STATE_PROCESS_SAMPLE` 在校准模式下的分支：
  - 把 `filtered_code` 累加到 `calibration_accumulator`
  - `calibration_count++`
  - 如果 `calibration_count < APP_DARK_CALIBRATION_SAMPLES`
    - 返回 `APP_STATE_WAIT_DRDY`
    - 继续采集下一帧校准样本
  - 如果 `calibration_count >= APP_DARK_CALIBRATION_SAMPLES`
    - 计算平均值得到 `baseline_code`
    - 清空滤波器有效标志，避免运行态继承校准阶段滤波历史
    - 把 `pipeline_mode` 切换为 `APP_PIPELINE_RUN`
    - 清空校准期间累计的 TIM6 输出许可，再返回 `APP_STATE_WAIT_DRDY`

这里有一个实现细节需要特别说明：

- 校准完成后，并不会经过一个单独的“校准完成状态”
- 而是通过把 `pipeline_mode` 从 `APP_PIPELINE_CALIBRATION` 改成 `APP_PIPELINE_RUN`
- 让后续 DRDY 样本自动进入正常运行流程

### 4. 正常采样运行阶段

校准完成后的稳定循环如下：

`APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE -> APP_STATE_WAIT_TRIGGER -> APP_STATE_USB_FLUSH/WAIT_DRDY`

这就是设备长期运行时最核心的闭环。

- `APP_STATE_WAIT_TRIGGER`
  - 等待 `TIM6` 输出许可
  - 不启动 ADS1220 转换

- `APP_STATE_WAIT_DRDY`
  - 等待 continuous ADS1220 的下一个样本完成
  - `DRDY` 到来则进入读数
  - 超时则进入故障

- `APP_STATE_READ_SAMPLE`
  - 读取 24 位原始码值
  - 成功后进入处理
  - 失败则进入故障

- `APP_STATE_PROCESS_SAMPLE`
  - 对原始值做 IIR 滤波
  - 用 `baseline_code` 做基线扣除，得到 `corrected_code`
  - 运行态按 `2 个 DRDY -> 1 个输出样本候选` 显式降采样，然后进入 `APP_STATE_WAIT_TRIGGER`

- `APP_STATE_USB_FLUSH`
  - 调用 `frame_builder_build()` 打包 420 字节 `frame_packet_t`
  - 通过 `usb_stream_enqueue_frame()` 压入图像帧发送队列
  - 随后回到 `APP_STATE_WAIT_DRDY`

因此，正常运行阶段可以概括为：

- `DRDY` 决定“何时可以读取最新 ADC 结果”
- `TIM6` 决定“何时允许输出一个 1 kHz 运行样本”
- 主循环负责“读数、处理、打包、排队发送”

### 5. 故障阶段

任意阶段发现关键错误时，V0.2 会先把底层状态映射为 `diag_fault_code_t`，再交给 `fault_policy` 决定是否自动恢复。只有超过恢复阈值或遇到不可恢复初始化错误时，才进入 `APP_STATE_FAULT`。

当前实现中的典型故障来源主要包括：

- `APP_STATE_INIT` 中 ADC 或 DAC 初始化失败
- `APP_STATE_WAIT_DRDY` 中等待 `DRDY` 超时
- `APP_STATE_READ_SAMPLE` 中 SPI 读数失败
- `APP_STATE_PROCESS_SAMPLE` 中通信自检失败
- `usb_stream` 样本或辅助队列溢出
- `switch` 落入默认分支

自动恢复策略：

- `SPI timeout`
  停止 continuous 后重新启动当前流水线的 continuous 链路，最多 `APP_RECOVERY_SPI_RETRY_LIMIT` 次
- `config mismatch`
  执行 `reset + configure + link_check`，成功后回到 `BIAS_STABILIZE`，重新自检和校准
- `DRDY timeout`
  按 ADC 重配策略恢复
- `usb busy overflow`
  不拉停采样，仅记录 `DIAG_FAULT_USB_BUSY_OVERFLOW` 并在保留的新帧置 `SAMPLE_FLAG_USB_OVERFLOW`
- 连续恢复失败超过 `APP_RECOVERY_HOLD_THRESHOLD`
  进入 `APP_STATE_FAULT`

进入 `APP_STATE_FAULT` 后：

- 不再恢复到采样状态
- 只通过 `app_handle_fault_reporting()` 周期性构造故障帧
- 故障帧继续通过 `usb_stream_enqueue_aux()` 进入辅助发送队列

也就是说，当前版本的故障态是“恢复失败后的保持态”：

- 会保留最近故障码、计数、复位原因和恢复动作结果
- 会周期性上报
- 不再自动重试，等待外部复位或人工处理

### 6. 一张简化的跳转图

把以上流程压缩后，可以得到一张更接近代码行为的跳转图：

`INIT -> BIAS_STABILIZE -> COMM_CHECK -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE`

从 `PROCESS_SAMPLE` 分三路：

- 自检成功：`PROCESS_SAMPLE -> DARK_CALIBRATE`
- 自检失败：`PROCESS_SAMPLE -> RECOVER -> BIAS_STABILIZE / FAULT`
- 非自检模式：按 `pipeline_mode` 进入校准或运行闭环

校准闭环：

`DARK_CALIBRATE -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE -> WAIT_DRDY`

运行闭环：

`WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE -> WAIT_TRIGGER -> USB_FLUSH / WAIT_DRDY`

异常收口：

- `WAIT_DRDY / READ_SAMPLE / PROCESS_SAMPLE -> RECOVER`
- `RECOVER / INIT / default -> FAULT`，仅在连续恢复失败或不可恢复错误时进入

## ADS1220 接口约定

当前工程按以下协议实现：

- SPI 模式：`Mode 1`
  即 `CPOL = 0, CPHA = 1`
- 数据就绪：`DRDY` 低有效，连接 `PB0 / EXTI0`
- 读取方式：
  `DRDY` 下降沿到来后，直接读取 3 字节，不额外发送 `RDATA`
- 数据格式：
  24 位二进制补码，`MSB first`
- 片选约束：
  `CS` 在一次 SPI 事务期间保持低电平，空闲时保持高电平

当前默认配置寄存器定义位于 `Core/Inc/adc_protocol.h`：

- `ADS1220_DEFAULT_CONFIG0`
- `ADS1220_DEFAULT_CONFIG1`
- `ADS1220_DEFAULT_CONFIG2`
- `ADS1220_DEFAULT_CONFIG3`

如果后续需要切换通道、增益、数据率或参考源，优先修改这些默认值，或者新增一组显式配置表。

## ADS1220 4 个配置寄存器位域说明

下面按 `CONFIG0 ~ CONFIG3` 逐字节说明当前默认值的位域含义。

### CONFIG0 = `0x08`

二进制：

`0000 1000`

位域说明：

- `bit[7:4] MUX = 0000`
  选择默认差分输入通道
- `bit[3:1] GAIN = 100`
  当前默认配置对应固定一档 PGA 增益
- `bit[0] PGA_BYPASS = 0`
  PGA 不旁路，保持使能

说明：

- 这个字节主要决定“采哪个通道”和“前端增益多大”
- 如果后续更改传感器接法或量程，优先检查这个寄存器

### CONFIG1 = `0xD4`

二进制：

`1101 0100`

位域说明：

- `bit[7:5] DR = 110`
  turbo mode 下对应 `2000 SPS`
- `bit[4:3] MODE = 10`
  使用 turbo mode
- `bit[2] CM = 1`
  使用 continuous conversion mode
- `bit[1] TS = 0`
  普通模拟输入转换，不启用温度传感器模式
- `bit[0] BCS = 0`
  烧断电流源关闭

说明：

- 这个字节决定采样速率、工作模式和连续/单次转换模式
- 当前运行态要求 `DRDY` 以约 `2 kSPS` 节奏提供新样本；若 `DRDY` 周期与预期不一致，优先检查这里

### CONFIG2 = `0x10`

二进制：

`0001 0000`

位域说明：

- `bit[7:6] VREF = 00`
  参考源使用当前默认设置
- `bit[5:4] FIR = 01`
  数字滤波/抑制方式使用当前默认档位
- `bit[3] PSW = 0`
  低边开关保持关闭
- `bit[2] IDAC = 0`
  激励电流功能未启用
- `bit[1:0] BCS = 00`
  CRC、烧断检测等附加功能保持关闭

说明：

- 这个字节主要影响参考源、数字滤波行为和附加检测功能
- 如果后续做电桥、RTD 或激励电流类应用，这里通常需要重新配置

### CONFIG3 = `0x00`

二进制：

`0000 0000`

位域说明：

- `bit[7:5] I1MUX = 000`
  IDAC1 默认不路由到外部引脚
- `bit[4:2] I2MUX = 000`
  IDAC2 默认不路由到外部引脚
- `bit[1] DRDYM = 0`
  `DRDY` 保持为数据就绪输出功能
- `bit[0] RESERVED = 0`
  保持默认值

说明：

- 这个字节主要和 IDAC 路由、引脚复用行为有关
- 当前工程作为普通电压采样使用，因此保持全默认

### 当前默认配置总结

- 使用默认输入通道和默认增益启动
- 数据率和工作模式为 turbo + `2000 SPS` + continuous conversion
- 不启用温度模式
- 不启用 IDAC、电流激励、CRC 或烧断检测
- `DRDY` 作为普通数据就绪信号，由 MCU 通过 `EXTI0` 监测

这组默认值的设计目标是先保证：

- `DRDY -> 读 3 字节 -> 补码解析` 这条主链路稳定
- 后续可以在不改状态机结构的前提下，只通过寄存器值迭代前端配置

## 数据处理链

一次有效样本的处理顺序如下：

1. `adc_protocol_read_sample()` 读回 ADS1220 原始码值
2. `adc_protocol_parse_raw24()` 做 24 位补码符号扩展
3. `app_filter_raw_code()` 做整数 IIR 滤波
4. 暗态校准完成后，用 `baseline_code` 做基线扣除
5. 使用 DWT 周期计数器生成微秒级时间戳
6. 打包成固定 420 字节 `frame_packet_t`
7. 压入 `usb_stream` 图像帧队列，等待 USB CDC 发送

## USB 数据帧

主图像帧定义位于 `Core/Inc/frame_protocol.h`，固定 420 字节：

- `frame_header_t`：20 字节
- `pixels[100]`：400 字节，`int32_t` row-major

其中 `frame_id` 字段承载正常图像流序号，协议约定如下：

- `frame_id` 类型为 `uint32_t`
- 每发送 1 个正常图像帧递增 `1`
- 到 `UINT32_MAX` 后回绕到 `0`
- 主机端做连续性校验时必须支持这种 `uint32_t` wrap-around

V0.x 的 32 字节 `sample_packet_t` 仍保留给元信息和故障帧，走独立辅助队列，不打断主图像帧队列。完整帧格式见 `docs/v1_frame_protocol.md`。

当前 ADS1220 采样节拍仍由 `APP_SAMPLE_RATE_HZ = 1000` 驱动；V1.0 图像输出按 `LOGICAL_FRAME_RATE_HZ = 100` 节流，每 10 个运行态样本构造并发送 1 个 10x10 图像帧。

## 中断设计原则

- `EXTI0_IRQHandler()`
  只清 `DRDY` 中断并调用 `app_on_drdy_isr()`
- `TIM6_DAC_IRQHandler()`
  只清更新中断并调用 `app_on_sample_tick_isr()`
- 主循环 `app_run_once()`
  统一完成 SPI 读数、状态机切换、数据处理和 USB 发送

这样做的目的是避免在 ISR 中执行阻塞式 SPI、USB 发送或复杂计算，降低系统耦合和时序风险。

## 关键配置参数

主要运行参数集中在 `Core/Inc/app_config.h`：

- `APP_SAMPLE_RATE_HZ`
- `APP_BIAS_STABILIZE_MS`
- `APP_DARK_CALIBRATION_SAMPLES`
- `APP_DRDY_TIMEOUT_MS`
- `APP_FILTER_ALPHA_SHIFT`
- `APP_USB_QUEUE_DEPTH`
- `APP_RECOVERY_SPI_RETRY_LIMIT`
- `APP_RECOVERY_RECONFIGURE_LIMIT`
- `APP_RECOVERY_HOLD_THRESHOLD`
- `APP_ADC_LINK_CHECK_RETRIES`
- `APP_DAC_BIAS_CH1`
- `APP_DAC_BIAS_CH2`

## 硬件引脚映射

定义位于 `Core/Inc/main.h`：

- `PB0  -> ADC_DRDY`
- `PB1  -> ADC_RST`
- `PB2  -> ADC_START`
- `PA15 -> ADC_CS`
- `PB3  -> SPI1_SCK`
- `PB4  -> SPI1_MISO`
- `PB5  -> SPI1_MOSI`
- `PA4  -> DAC1_OUT1`
- `PA5  -> DAC1_OUT2`

## 维护建议

- 需要改采样流程时，优先查看 `Core/Src/app.c`
- 需要改 ADS1220 寄存器和时序时，优先查看 `Core/Src/adc_protocol.c`
- 需要改 PC 通信格式时，优先查看 `Core/Inc/usb_stream.h` 和 `Core/Src/usb_stream.c`
- 需要改中断行为时，只允许在 ISR 中做“置标志”和“清中断”，不要直接做 SPI/USB 阻塞操作
- 如果重新用 CubeMX 生成代码，注意检查 `TIM6`、`EXTI0`、`ADC_CS` 空闲电平、SPI 相位极性是否被覆盖

## 已知边界

- 温度模式解析尚未单独实现，当前按普通 24 位模拟输入结果处理
- 项目当前未使用 DMA，SPI 和 USB 都由主循环轮询推进
- 自动恢复只覆盖单设备、无 DMA、无帧协议的 V0.2 范围；多设备仲裁和更复杂的主机协议仍未实现

## V0.2 交付物

- 故障码表：`docs/fault_codes.md`
- 恢复状态机图：`docs/recovery_state_machine.md`
- 异常注入测试记录：`docs/fault_injection_v0.2.0.md`
- 连续采样时序：`docs/continuous_sampling.md`

## V1.0 交付物

- 固定 100 pixels 拓扑说明：`docs/v1_topology.md`
- 整帧协议说明：`docs/v1_frame_protocol.md`
- 上位机最小显示程序：`tools/host_viewer/viewer.py`
- 帧解析模块：`tools/host_viewer/frame_decoder.py`

