# STM32G431CBT_MCU

基于 `STM32G431CBT`、`ADS1220` 和 `USB CDC` 的采样工程。

当前代码已经按“CubeMX 负责底层初始化，应用层负责状态机”的方式重构，主流程为：

`初始化 -> 偏置稳定 -> 通信自检 -> 暗态校准 -> 定时触发采样 -> DRDY 读数 -> 数据处理 -> USB 发送`

## 目录结构

- `Core/Src/main.c`
  CubeMX 生成的主入口，负责时钟、GPIO、SPI、DAC、TIM6、USB 初始化，并在 `while(1)` 中调用 `app_run_once()`
- `Core/Src/app.c`
  应用状态机核心，负责系统启动流程、采样调度、暗态校准、滤波、基线扣除、时间戳和数据打包
- `Core/Src/adc_protocol.c`
  ADS1220 驱动层，负责 `RESET / START/SYNC / POWERDOWN / WREG / RREG / 读3字节 / 补码解析`
- `Core/Src/usb_stream.c`
  USB CDC 发送队列，负责 32 字节样本帧管理和 64 字节 FS 包聚合发送
- `Core/Src/stm32g4xx_it.c`
  中断入口，`EXTI0` 只置 `DRDY` 事件，`TIM6_DAC` 只置采样节拍事件，`USB_LP` 交给 Cube USB 栈
- `USB_Device/App/usbd_cdc_if.c`
  USB CDC 适配层，当前主要用于把应用层缓冲区发到上位机

## 状态机说明

应用层状态定义在 `Core/Inc/app.h`：

- `APP_STATE_INIT`
  复位 ADS1220，写默认寄存器，启动 DAC 默认偏置
- `APP_STATE_BIAS_STABILIZE`
  等待模拟前端稳定
- `APP_STATE_COMM_CHECK`
  发起一次试采样，并用寄存器回读确认 ADS1220 通信链路正常
- `APP_STATE_DARK_CALIBRATE`
  采集固定数量样本建立暗态基线
- `APP_STATE_WAIT_TRIGGER`
  等待 TIM6 周期节拍
- `APP_STATE_WAIT_DRDY`
  等待 ADS1220 `DRDY` 下降沿
- `APP_STATE_READ_SAMPLE`
  读取 ADS1220 24 位原始结果
- `APP_STATE_PROCESS_SAMPLE`
  做 IIR 滤波、基线扣除、序号和时间戳更新
- `APP_STATE_USB_FLUSH`
  打包为 32 字节二进制帧并压入 USB 队列
- `APP_STATE_FAULT`
  停止采样并周期性上报故障帧

## ADS1220 接口约定

当前工程按以下协议实现：

- SPI 模式：`Mode 1`
  即 `CPOL=0, CPHA=1`
- 数据就绪：`DRDY` 低有效，接 `PB0 / EXTI0`
- 读取方式：
  `DRDY` 下降沿到来后，直接读取 3 字节，不额外发送 `RDATA`
- 数据格式：
  24 位二进制补码，`MSB first`
- 片选约束：
  `CS` 在整个串口事务期间保持低，空闲保持高

当前默认配置寄存器定义在 `Core/Inc/adc_protocol.h`：

- `ADS1220_DEFAULT_CONFIG0`
- `ADS1220_DEFAULT_CONFIG1`
- `ADS1220_DEFAULT_CONFIG2`
- `ADS1220_DEFAULT_CONFIG3`

如果后续需要切换通道、增益、数据率或参考源，优先修改这些默认值，或者新增一组显式配置表。

## 数据处理链

当前运行态的处理链如下：

1. ADS1220 输出 24 位原始码
2. `adc_protocol_parse_raw24()` 做符号扩展
3. `app_filter_raw_code()` 做整数 IIR 滤波
4. 暗态校准完成后，用 `baseline_code` 做基线扣除
5. 使用 DWT 周期计数器生成微秒级时间戳
6. 打包成固定 32 字节 `sample_packet_t`
7. 入 `usb_stream` 队列，等待 USB CDC 发送

## USB 数据帧

样本帧定义在 `Core/Inc/usb_stream.h`，固定 32 字节：

- `magic`
- `version`
- `state`
- `flags`
- `reserved`
- `sequence`
- `timestamp_us`
- `raw_code`
- `filtered_code`
- `baseline_code`
- `corrected_code`

两个样本帧会被优先拼成一个 64 字节 USB FS 包，减少传输碎片。

## 中断设计原则

本工程有意保持“中断轻、主循环重”：

- `EXTI0_IRQHandler()`
  只清 DRDY 中断并调用 `app_on_drdy_isr()`
- `TIM6_DAC_IRQHandler()`
  只清更新中断并调用 `app_on_sample_tick_isr()`
- 主循环 `app_run_once()`
  完成 SPI 读数、状态切换、滤波、USB 入队

这样做的目的是避免在 ISR 中执行阻塞 SPI、USB 发送或复杂计算，降低系统耦合和时序风险。

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

如果需要调整采样率，优先修改：

- `APP_SAMPLE_RATE_HZ`
- `APP_TIM6_PRESCALER`
- `APP_TIM6_PERIOD`

## 当前默认硬件映射

定义在 `Core/Inc/main.h`：

- `PB0`  : `ADC_DRDY`
- `PB1`  : `ADC_RST`
- `PB2`  : `ADC_START`
- `PA15` : `ADC_CS`
- `PB3`  : `SPI1_SCK`
- `PB4`  : `SPI1_MISO`
- `PB5`  : `SPI1_MOSI`
- `PA4`  : `DAC1_OUT1`
- `PA5`  : `DAC1_OUT2`

## 维护建议

- 需要改采样流程时，优先看 `Core/Src/app.c`
- 需要改 ADS1220 寄存器和时序时，优先看 `Core/Src/adc_protocol.c`
- 需要改 PC 通信格式时，优先看 `Core/Inc/usb_stream.h` 和 `Core/Src/usb_stream.c`
- 需要改中断行为时，只允许在 ISR 中做“置标志”和“清中断”，不要直接做 SPI/USB 阻塞操作
- 如果重新用 CubeMX 生成代码，注意检查 `TIM6`、`EXTI0`、`ADC_CS` 空闲电平、SPI 相位极性是否被覆盖

## 已知边界

- 当前默认配置值仅保证驱动框架和状态机链路完整，不代表最终传感器量程配置
- 温度模式解析尚未单独实现，当前按普通 24 位模拟输入结果处理
- 项目当前未使用 DMA，SPI 和 USB 都由主循环轮询推进
- 故障态默认不自动恢复，只周期性发送故障状态帧
