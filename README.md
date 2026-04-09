# STM32G431CBT_MCU

基于 `STM32G431CBT`、`ADS1220` 和 `USB CDC` 的采样工程。

当前工程采用“`CubeMX` 负责底层外设初始化，应用层负责状态机调度”的分层方式。系统主流程如下：

`初始化 -> 偏置稳定 -> 通信自检 -> 暗态校准 -> 定时触发采样 -> DRDY 读数 -> 数据处理 -> USB 发送`

## 目录结构

- `Core/Src/main.c`
  CubeMX 生成的主入口，负责时钟、GPIO、SPI、DAC、TIM6、USB 初始化，并在 `while(1)` 中调用 `app_run_once()`
- `Core/Src/app.c`
  应用状态机核心，负责系统启动流程、采样调度、暗态校准、滤波、基线扣除、时间戳和数据打包
- `Core/Src/adc_protocol.c`
  ADS1220 驱动层，负责 `RESET / START/SYNC / POWERDOWN / WREG / RREG / 读 3 字节 / 补码解析`
- `Core/Src/usb_stream.c`
  USB CDC 发送层，负责样本/辅助双队列管理、32 字节帧打包和 64 字节 FS 包优先聚合发送
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
  打包成 32 字节二进制帧并压入 USB 队列
- `APP_STATE_FAULT`
  停止采样，周期性发送故障状态帧

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
    - 任一 ADC 配置失败或 DAC 启动失败，进入 `APP_STATE_FAULT`

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
    - 调用 `app_begin_conversion(APP_PIPELINE_COMM_CHECK)`
    - 发起一次试采样
    - 切换到 `APP_STATE_WAIT_DRDY`
  - 作用：
    - 用和正常采样相同的链路验证 `START -> DRDY -> SPI 读数 -> 寄存器回读校验` 是否完整可用

### 2. 通信自检阶段

通信自检不是一个单独的“读完就结束”的状态，而是借用正常采样链路跑一遍：

`APP_STATE_COMM_CHECK -> APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE`

此时 `pipeline_mode = APP_PIPELINE_COMM_CHECK`，因此 `APP_STATE_PROCESS_SAMPLE` 会走“自检分支”。

- `APP_STATE_WAIT_DRDY`
  - 进入条件：
    - 来自 `APP_STATE_COMM_CHECK`
    - 或来自后续运行阶段的 `APP_STATE_WAIT_TRIGGER`
  - 执行动作：
    - 等待 `DRDY` 中断把 `evt_drdy` 置位
  - 正常退出：
    - 收到 `DRDY` 后进入 `APP_STATE_READ_SAMPLE`
  - 异常退出：
    - 若超出 `APP_DRDY_TIMEOUT_MS` 仍未等到 `DRDY`，进入 `APP_STATE_FAULT`
    - 如果当前是通信自检模式，还会额外打上 `SAMPLE_FLAG_COMM_CHECK_FAILED`

- `APP_STATE_READ_SAMPLE`
  - 进入条件：`DRDY` 已到来
  - 执行动作：
    - 在主循环中调用 `adc_protocol_read_sample()` 读取 ADS1220 原始 24 位结果
  - 正常退出：
    - SPI 读数成功后进入 `APP_STATE_PROCESS_SAMPLE`
  - 异常退出：
    - SPI 读数失败则进入 `APP_STATE_FAULT`
    - 如果当前是通信自检模式，还会额外打上 `SAMPLE_FLAG_COMM_CHECK_FAILED`

- `APP_STATE_PROCESS_SAMPLE`
  - 进入条件：已经得到一帧原始码值
  - 执行动作：
    - 先统一执行一次 `app_filter_raw_code()`
    - 然后根据 `pipeline_mode` 决定后续跳转
  - 自检分支：
    - 调用 `adc_protocol_link_check()` 回读 ADS1220 配置寄存器
    - 如果回读结果和期望寄存器镜像一致，说明通信链路正常，进入 `APP_STATE_DARK_CALIBRATE`
    - 如果回读失败或不一致，进入 `APP_STATE_FAULT`

因此，自检阶段的结论很明确：

- 自检成功：
  `APP_STATE_PROCESS_SAMPLE -> APP_STATE_DARK_CALIBRATE`
- 自检失败：
  `APP_STATE_PROCESS_SAMPLE -> APP_STATE_FAULT`

### 3. 暗态校准阶段

暗态校准由 `APP_STATE_DARK_CALIBRATE` 启动，但真正的样本累计是在后续 `WAIT_TRIGGER -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE` 这一循环里完成。

主链路如下：

`APP_STATE_DARK_CALIBRATE -> APP_STATE_WAIT_TRIGGER -> APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE -> APP_STATE_WAIT_TRIGGER`

- `APP_STATE_DARK_CALIBRATE`
  - 进入条件：通信自检成功
  - 执行动作：
    - 设置 `pipeline_mode = APP_PIPELINE_CALIBRATION`
    - 清空校准累加器、样本计数、滤波器历史和基线值
    - 启动 `TIM6`
  - 正常退出：
    - 进入 `APP_STATE_WAIT_TRIGGER`

- `APP_STATE_WAIT_TRIGGER`
  - 进入条件：
    - 来自 `APP_STATE_DARK_CALIBRATE`
    - 或来自校准/运行阶段一次样本处理结束后
  - 执行动作：
    - 等待 `TIM6` 中断置位 `evt_sample_tick`
  - 正常退出：
    - 收到采样节拍后调用 `app_begin_conversion(g_app.pipeline_mode)`，进入 `APP_STATE_WAIT_DRDY`

- `APP_STATE_PROCESS_SAMPLE` 在校准模式下的分支：
  - 把 `filtered_code` 累加到 `calibration_accumulator`
  - `calibration_count++`
  - 如果 `calibration_count < APP_DARK_CALIBRATION_SAMPLES`
    - 返回 `APP_STATE_WAIT_TRIGGER`
    - 继续采集下一帧校准样本
  - 如果 `calibration_count >= APP_DARK_CALIBRATION_SAMPLES`
    - 计算平均值得到 `baseline_code`
    - 清空滤波器有效标志，避免运行态继承校准阶段滤波历史
    - 把 `pipeline_mode` 切换为 `APP_PIPELINE_RUN`
    - 再返回 `APP_STATE_WAIT_TRIGGER`

这里有一个实现细节需要特别说明：

- 校准完成后，并不会经过一个单独的“校准完成状态”
- 而是通过把 `pipeline_mode` 从 `APP_PIPELINE_CALIBRATION` 改成 `APP_PIPELINE_RUN`
- 让下一次 `APP_STATE_WAIT_TRIGGER` 开始的采样自动进入正常运行流程

### 4. 正常采样运行阶段

校准完成后的稳定循环如下：

`APP_STATE_WAIT_TRIGGER -> APP_STATE_WAIT_DRDY -> APP_STATE_READ_SAMPLE -> APP_STATE_PROCESS_SAMPLE -> APP_STATE_USB_FLUSH -> APP_STATE_WAIT_TRIGGER`

这就是设备长期运行时最核心的闭环。

- `APP_STATE_WAIT_TRIGGER`
  - 等待 `TIM6` 周期节拍
  - 节拍到来后启动一次 ADS1220 转换

- `APP_STATE_WAIT_DRDY`
  - 等待 ADS1220 转换完成
  - `DRDY` 到来则进入读数
  - 超时则进入故障

- `APP_STATE_READ_SAMPLE`
  - 读取 24 位原始码值
  - 成功后进入处理
  - 失败则进入故障

- `APP_STATE_PROCESS_SAMPLE`
  - 对原始值做 IIR 滤波
  - 用 `baseline_code` 做基线扣除，得到 `corrected_code`
  - 然后进入 `APP_STATE_USB_FLUSH`

- `APP_STATE_USB_FLUSH`
  - 调用 `app_build_sample_packet()` 打包 32 字节样本帧
  - 通过 `usb_stream_enqueue_sample()` 压入样本发送队列
  - 随后回到 `APP_STATE_WAIT_TRIGGER`

因此，正常运行阶段可以概括为：

- `TIM6` 决定“何时开始一次采样”
- `DRDY` 决定“何时可以读取这一帧结果”
- 主循环负责“读数、处理、打包、排队发送”

### 5. 故障阶段

任意阶段只要发现关键错误，都会统一调用 `app_enter_fault()` 进入 `APP_STATE_FAULT`。

当前实现中的故障来源主要包括：

- `APP_STATE_INIT` 中 ADC 或 DAC 初始化失败
- `APP_STATE_WAIT_DRDY` 中等待 `DRDY` 超时
- `APP_STATE_READ_SAMPLE` 中 SPI 读数失败
- `APP_STATE_PROCESS_SAMPLE` 中通信自检失败
- `switch` 落入默认分支

`app_enter_fault()` 会做以下动作：

- 记录故障标志位
- 停止 `TIM6`
- 调用 `adc_protocol_stop()` 停止 ADS1220 转换
- 清空 `evt_sample_tick` 和 `evt_drdy`
- 设置 `last_fault_report_ms`
- 切换状态到 `APP_STATE_FAULT`

进入 `APP_STATE_FAULT` 后：

- 不再恢复到采样状态
- 只通过 `app_handle_fault_reporting()` 周期性构造故障帧
- 故障帧继续通过 `usb_stream_enqueue_aux()` 进入辅助发送队列

也就是说，当前版本的故障态是“锁定式故障态”：

- 会保留故障信息
- 会周期性上报
- 不会自动重试恢复

### 6. 一张简化的跳转图

把以上流程压缩后，可以得到一张更接近代码行为的跳转图：

`INIT -> BIAS_STABILIZE -> COMM_CHECK -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE`

从 `PROCESS_SAMPLE` 分三路：

- 自检成功：`PROCESS_SAMPLE -> DARK_CALIBRATE`
- 自检失败：`PROCESS_SAMPLE -> FAULT`
- 非自检模式：按 `pipeline_mode` 进入校准或运行闭环

校准闭环：

`DARK_CALIBRATE -> WAIT_TRIGGER -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE -> WAIT_TRIGGER`

运行闭环：

`WAIT_TRIGGER -> WAIT_DRDY -> READ_SAMPLE -> PROCESS_SAMPLE -> USB_FLUSH -> WAIT_TRIGGER`

异常收口：

- `INIT / WAIT_DRDY / READ_SAMPLE / PROCESS_SAMPLE / default -> FAULT`

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

### CONFIG1 = `0x04`

二进制：

`0000 0100`

位域说明：

- `bit[7:5] DR = 000`
  数据率使用默认档位
- `bit[4:2] MODE = 001`
  当前工程使用普通转换工作模式
- `bit[1:0] CM = 00`
  转换模式保持默认配置

说明：

- 这个字节主要决定采样速率和工作模式
- 如果后续发现 `DRDY` 周期与预期不一致，优先检查这里

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
- 数据率和工作模式保持保守默认值
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
6. 打包成固定 32 字节 `sample_packet_t`
7. 压入 `usb_stream` 队列，等待 USB CDC 发送

## USB 数据帧

样本帧定义位于 `Core/Inc/usb_stream.h`，固定 32 字节：

- `magic : uint16_t`
- `version : uint8_t`
- `state : uint8_t`
- `flags : uint16_t`
- `reserved : uint16_t`
- `sequence : uint32_t`
- `timestamp_us : uint32_t`
- `raw_code : int32_t`
- `filtered_code : int32_t`
- `baseline_code : int32_t`
- `corrected_code : int32_t`

样本帧会优先按两个 32 字节拼成一个 64 字节 USB FS 包；如果只剩 1 帧，则等待最多 `APP_USB_BATCH_MAX_WAIT_MS` 后再单独发出。元信息和故障帧使用独立辅助队列，不会把最旧样本挤掉。

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
- 故障态默认不自动恢复，只周期性发送故障状态帧

