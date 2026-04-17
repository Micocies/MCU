# MCU_STM32G431BT 阵列成像项目路线图

## 项目目标

实现一个固定 **10×10 = 100 pixels** 的阵列采集演示系统，采用：

- STM32G431BT 主控
- ADS1220 多设备轮询采集
- 帧扫描方式
- USB 输出整帧数据
- 上位机实时显示 10×10 图像
- 后续升级到 DMA / 双缓冲架构

## 一、总原则

### 1. 固定最终目标，不做无限泛化

本项目不是通用 ADC 框架库，而是一个固定拓扑的 **100 pixels 成像演示系统**。
因此从 V1.0 开始冻结以下全局约束：

- 阵列尺寸固定为 `10 x 10`
- 总像素数固定为 `100`
- 总 ADS1220 数量固定为 `25`
- 每颗 ADS1220 负责 `4` 路单端输入
- 总子板数固定为 `5`
- 每块子板负责 `20 pixels`
- 目标像素采样率固定为 `100 Hz / pixel`
- 逻辑帧率目标固定为 `100 fps`
- 采用**帧扫描**，不要求严格同步采样
- USB 输出以**帧**为基本单位
- DMA 不进入 V1.x，待闭环跑通后再引入

### 2. 开发顺序：先闭环，再扩规模，再做性能

必须遵循以下顺序：

1. 先打通最小完整链路
   `MCU -> USB -> Host -> 图像显示`
2. 再接入真实多设备轮询
3. 再扩展到完整 100 pixels
4. 最后在已验证正确性的前提下引入 DMA / 双缓冲

### 3. 帧定义必须从第一版冻结

本项目采用**逻辑帧**定义：

> 一个 `10 ms` 时间窗内，100 个 pixel 各更新一次，构成一帧。

说明：

- 该帧不是“100 点严格同时曝光”
- 该帧是“扫描式整帧”
- 上位机按 `frame_id + pixel_id` 重建图像

---

## 二、全局冻结配置

建议新增 `Core/Inc/project_config.h`

```c
#pragma once

#define ARRAY_WIDTH              10
#define ARRAY_HEIGHT             10
#define PIXEL_COUNT              100

#define ADC_DEVICE_COUNT         25
#define ADC_CHANNELS_PER_DEVICE  4

#define SUBBOARD_COUNT           5
#define ADC_PER_SUBBOARD         5
#define PIXELS_PER_SUBBOARD      20

#define TARGET_PIXEL_RATE_HZ     100
#define LOGICAL_FRAME_RATE_HZ    100
#define FRAME_PERIOD_MS          10

## 三、固定硬件拓扑

### 1. 总体结构

- 主控：STM32G431BT
- ADC：25 个 ADS1220
- 子板：5 块
- 每块子板：
    - 5 个 ADS1220
    - 20 个 pixels
- 每个 ADS1220：
    - 4 路单端输入
- 主控与 ADC：
    - 共享 SPI 总线
    - 轮询访问
    - 每设备独立 CS
    - START / RST 可共享
    - DRDY 第一阶段只等待“当前活动设备”

### 2. 片选扩展策略

- 设备数 ≤ 8：可直接 GPIO CS
- 设备数 9～32：优先使用 `74HC138 / 74HC595 / GPIO expander`
- 当前固定为 25 设备，因此允许使用扩展方案
- 若未来超过当前规模，必须重新评估架构，而不是直接平铺复制

### 3. DRDY 策略

第一阶段：

- 不做所有 DRDY 并发 EXTI
- 只等待当前活动设备的 DRDY
- 允许通过轮询方式实现

后续优化阶段可选：

- 分组 DRDY
- 共享 START 后顺序读取
- 更复杂同步策略

---

## 四、目录与模块规划

### 新增目录 / 文件建议

### 全局配置与拓扑

- `Core/Inc/project_config.h`
- `Core/Inc/board_topology.h`
- `Core/Src/board_topology.c`

### 像素与帧协议

- `Core/Inc/pixel_map.h`
- `Core/Src/pixel_map.c`
- `Core/Inc/frame_protocol.h`
- `Core/Src/frame_protocol.c`
- `Core/Inc/frame_builder.h`
- `Core/Src/frame_builder.c`

### ADC 总线与设备层

- `Core/Inc/adc_bus.h`
- `Core/Src/adc_bus.c`
- `Core/Inc/ads1220_device.h`
- `Core/Src/ads1220_device.c`
- `Core/Inc/ads1220_scheduler.h`
- `Core/Src/ads1220_scheduler.c`

### 性能与缓冲（V2.0 起）

- `Core/Inc/ring_buffer.h`
- `Core/Src/ring_buffer.c`
- `Core/Inc/frame_queue.h`
- `Core/Src/frame_queue.c`
- `Core/Inc/sample_scheduler.h`
- `Core/Src/sample_scheduler.c`

### 上位机工具

- `tools/host_viewer/frame_decoder.py`
- `tools/host_viewer/viewer.py`

---

## 五、版本计划

---

# V1.0 固定 100 pixels 最小完整链路版

## 目标

在**固定 100 pixels 最终接口不变**的前提下，先打通最小完整链路：

`MCU -> USB -> Host -> 10x10 图像显示`

## Why

在项目早期，最重要的不是先做出“通用多设备抽象”，

而是先验证：

- 帧协议是否合理
- USB 输出是否稳定
- 上位机能否正确解析
- 图像显示链路是否闭环
- 后续真实采样值能否无缝接入

## 关键决策

- 从 V1.0 开始，MCU 对外就输出**固定 100 pixels 帧**
- 即使硬件暂时只接入部分真实通道，也要维持最终帧格式不变
- 未接入位置可填充测试值、占位值或重复值
- 上位机从第一版开始按固定 10×10 解析

## 本版范围

- 冻结 `100 pixels` 全局配置
- 定义 `pixel_id`
- 定义固定像素映射表
- 定义整帧协议
- 实现 `frame_builder`
- 改造 `usb_stream.c`，从样本发送切到帧发送
- 编写上位机 MVP 显示程序
- 支持测试帧 / 半真实帧 / 占位帧输出

## 本版不做

- 完整 25 设备真实接入
- DMA
- 真同步采样
- 多 DRDY 并发中断
- 无限泛化设备抽象

## 建议帧结构

```c
typedef struct {
    uint16_t magic;
    uint8_t  version;
    uint8_t  frame_type;
    uint32_t frame_id;
    uint16_t width;
    uint16_t height;
    uint32_t timestamp_us;
    uint16_t payload_bytes;
    uint16_t crc16;
} frame_header_t;
```

帧 payload 第一阶段固定为：

```c
int32_t pixels[100];
```

## 修改文件

新增：

- `project_config.h`
- `board_topology.h/.c`
- `pixel_map.h/.c`
- `frame_protocol.h/.c`
- `frame_builder.h/.c`
- `tools/host_viewer/frame_decoder.py`
- `tools/host_viewer/viewer.py`

修改：

- `app.c`
- `usb_stream.c`

## 退出标准

- 上位机可持续显示 10×10 图像
- 连续 1000 帧无异常跳变
- 指定测试 pixel 位置显示正确
- USB 链路稳定，不死机
- 后续版本无需重写帧协议

## 输出物

- `v1.0.0` tag
- 100 pixels 固定拓扑说明
- 帧协议说明书
- 上位机最小显示程序
- 最小闭环演示视频

---
# V1.1 固定 100 pixels 多设备抽象与轮询版

## 目标

在 **V1.0 的固定 100 pixels 链路不变** 的前提下，

引入真实 ADS1220 多设备接入和轮询调度。

## Why

这一版的重点是验证：

- 多设备能否稳定切换
- 配置是否彼此独立
- 切换时是否串道
- 数据能否正确落到对应 pixel 位置

## 关键决策

- 共享 SPI 总线
- 按设备轮询
- 可共享 START / RST
- 第一版只等待当前活动设备的 DRDY
- 不做所有设备 DRDY 并发 EXTI

## 本版范围

- 新增 bus 层
- 新增 device 层
- 新增固定 25 设备配置表
- 引入 `device_id`
- 引入 `subboard_id`
- 让 `pixel_map` 绑定真实硬件拓扑
- 支持多设备 reset/config/read

## 本版不做

- DMA
- 复杂同步
- 校准
- 图像增强

## 建议数据结构

```c
typedef struct {
    SPI_HandleTypeDef *hspi;
} adc_bus_t;

typedef struct {
    uint8_t device_id;
    uint8_t subboard_id;

    GPIO_TypeDef *cs_port;
    uint16_t cs_pin;

    GPIO_TypeDef *drdy_port;
    uint16_t drdy_pin;

    ads1220_config_t expected_config;
    int32_t last_raw_code;
    adc_protocol_status_t last_status;
} ads1220_device_t;
```

## 修改文件

新增：

- `adc_bus.h/.c`
- `ads1220_device.h/.c`
- `ads1220_scheduler.h/.c`

修改：

- `adc_protocol.c`
- `app.c`
- `main.h`
- `board_topology.c`

## 退出标准

- 至少 1 块子板（20 pixels）真实跑通
- 多设备切换 10000 次无串道
- 每设备都能独立 reset/config/read
- 映射关系正确
- V1.0 上位机无需修改即可显示真实输入

## 输出物

- `v1.1.0` tag
- 设备抽象层接口文档
- 子板拓扑文档
- 多设备轮询测试报告

---

# V1.2 固定 100 pixels 整机成帧 MVP 版

## 目标

把系统从“局部真实接入”扩展为：

`25 个 ADS1220 + 100 pixels + USB 整帧 + 上位机 10x10 显示`

## Why

到这一步，项目才真正从“多路采样验证”变成“100 pixels 成像演示系统”。

## 本版范围

- 接入全部 5 块子板
- 支持完整 100 pixels 扫描
- 固定 100 Hz / pixel
- 支持逻辑帧构建
- 支持上位机持续显示
- 支持原始帧保存
- 增加运行统计：
    - `frame_ok_count`
    - `frame_drop_count`
    - `adc_timeout_count`
    - `usb_busy_count`
    - `scan_overrun_count`

## 本版不做

- DMA
- 高级同步
- 校准
- 图像算法优化

## 修改文件

新增：

- `system_metrics.h/.c`

修改：

- `app.c`
- `frame_builder.c`
- `usb_stream.c`
- `tools/host_viewer/viewer.py`

## 退出标准

- 100 pixels 可完整成帧
- 上位机可持续显示 10×10 图像
- 连续 1000 帧无帧号跳变异常
- 指定像素刺激位置正确
- 原始帧可保存
- 系统可稳定演示

## 输出物

- `v1.2.0` tag
- 100 pixels MVP 演示视频
- 帧抓包样例
- 上位机说明
- 整机联调报告

---

# V2.0 DMA / 双缓冲性能版

## 目标

在 **V1.2 已稳定成帧** 的前提下，

解决采样搬运、发送阻塞、CPU 占用等性能问题。

## Why

DMA 不应在正确性未验证前提前引入。

它应该在“链路已经正确”之后用于解决“性能瓶颈”。

## 本版前提

只有满足以下条件才进入本版：

- V1.2 已稳定跑通
- 已完成性能基准测试
- 已明确瓶颈在 SPI / 搬运 / 发送
- 而不是逻辑设计错误

## 本版范围

- 建立性能基准
- 引入 SPI DMA
- 引入双缓冲 / 环形缓冲
- 采样与发送解耦
- 生产者-消费者模型
- scheduler / worker / transport 分层
- DMA-safe / IRQ-safe 队列改造

## 本版不做

- 产品化
- 图像质量增强
- 高级校准

## 必做基准测试

进入 DMA 开发前，必须记录：

- 阻塞式 SPI 读取单像素耗时
- 单设备 4 通道扫描耗时
- 单子板 20 pixels 成帧耗时
- 全 100 pixels 成帧耗时
- USB 单帧发送耗时
- 主循环 CPU 占用

## 修改文件

新增：

- `ring_buffer.h/.c`
- `frame_queue.h/.c`
- `sample_scheduler.h/.c`

修改：

- `.ioc`
- `stm32g4xx_it.c`
- `usb_stream.c`
- `app.c`
- `adc_bus.c`

## 退出标准

- 满足目标帧率
- 连续 30 分钟无丢帧、无乱序
- CPU 主循环负载明显下降
- DMA 与 USB 队列无并发错误
- 双缓冲无覆盖/溢出

## 输出物

- `v2.0.0` tag
- DMA 性能基准报告
- 双缓冲状态机图
- 并发模型说明
- DMA 联调记录

---

# V2.1 图像质量 / 校准 / 同步增强版

## 目标

在系统已稳定成像基础上，提升：

- 图像一致性
- 通道偏置一致性
- 固定图样误差
- 帧内时间可追踪性

## Why

成像系统后续真正的问题通常不是“有没有图”，

而是：

- 条纹
- 偏置不一致
- 坏点
- 固定图样噪声
- 设备间时间差

## 本版范围

- 零点校准
- 增益校准
- 坏点标记
- 固定图样校正
- 帧内时序标签
- 可选共享 START 同步策略

## 可选同步策略

A. 共享 START，同步开始转换，顺序读取

B. 继续轮询，但为像素增加统一时间标签

C. 若硬件允许，再做分组 DRDY

## 修改文件

新增：

- `calibration.h/.c`
- `image_pipeline.h/.c`

## 退出标准

- 同一均匀输入下固定条纹下降
- 偏置差可量化、可校正
- 坏点可标记
- 校准参数可保存/加载

## 输出物

- `v2.1.0` tag
- 校准流程文档
- 图像质量评估报告
- 同步策略说明

---

## 七、明确不推荐的做法

以下做法当前阶段不推荐：

- 一开始就做“支持任意 N 个设备”的无限抽象
- 一开始就做所有 DRDY 并发中断
- 在未打通整帧闭环前引入 DMA
- 继续沿用“每样本 32 字节”的旧传输组织
- 在拓扑未冻结时同时推进主控、协议、上位机三边大改

---

## 八、当前阶段立即执行事项

### 1. 先冻结 3 个核心头文件

- `project_config.h`
- `board_topology.h`
- `pixel_map.h`

### 2. 先推进 V1.0

优先完成：

- 固定帧协议
- USB 帧发送
- 上位机 10×10 显示

### 3. 再推进 V1.1

优先完成：

- bus 层
- device 层
- 单子板真实轮询

### 4. 不要提前进入 DMA

只有当：

- 闭环稳定
- 性能数据明确
- 确认瓶颈真实存在

才进入 V2.0