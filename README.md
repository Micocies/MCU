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
  USB CDC 发送队列，负责 32 字节样本帧管理和 64 字节 FS 包聚合发送
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

两个样本帧会优先拼成一个 64 字节 USB FS 包，减少传输碎片。

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

