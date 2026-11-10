# STM32F407 Industrial Gateway

基于 STM32F407ZGT6 的工业网关学习项目，包含 FreeRTOS 应用程序、独立 Bootloader 和 OTA 升级相关工具。应用程序包含 CAN 数据处理、OneNet 数据上报，以及通过 W25Q64 存储升级固件的代码。

## 项目组成

```text
App/                         STM32 应用程序
  Core/                      外设初始化、中断和 FreeRTOS 任务
  Drivers/                   HAL、CMSIS 和用户驱动
  Middlewares/               FreeRTOS 等中间件
  MDK-ARM/APP_TWO.uvprojx    应用程序 Keil 工程
  ESP8266Bridge/             ESP-01S TCP/UART 桥接程序
  WebOTA/                    OTA 网页及 API 说明
  APP_TWO.ioc                STM32CubeMX 配置
Bootloader/                  Bootloader 工程
  Drivers/user/              Flash、W25Q64 和 OTA 相关代码
  MDK-ARM/BOOTLOADER-TWO.uvprojx
  BOOTLOADER-TWO.ioc
```

## 功能概览

- **CAN 与 FreeRTOS**：应用程序使用任务处理 CAN 数据，具体实现位于 `App/Drivers/user/`。
- **OneNet 上报**：应用程序包含 MQTT 连接和数据上报代码；设备参数使用占位值，需自行配置。
- **OTA 升级**：应用程序接收固件并写入 W25Q64；Bootloader 包含镜像校验、安装、备份和回滚相关逻辑。
- **ESP8266 桥接**：ESP-01S 在局域网中转发 TCP 和 UART 数据，接线及测试方法见 [ESP8266Bridge README](App/ESP8266Bridge/README.md)。
- **网页端**：`App/WebOTA/` 提供 OTA 页面和 API 约定，具体依赖见 [WebOTA README](App/WebOTA/README.md)。网页和桥接程序的协议与部署方式需要分别核对，不能直接假定两者可以配合使用。

## 开发环境

以下版本来自仓库中的 `.uvprojx` 和 `.ioc` 工程配置：

| 项目 | 配置 |
| --- | --- |
| 主控 | STM32F407ZGTx，Cortex-M4，LQFP144 |
| 开发工具 | Keil MDK-ARM（工程格式为 `.uvprojx`） |
| C 编译器 | ARM Compiler 5.06 Update 5，build 528（ARMCC 5，两个工程均选用此版本） |
| Keil 设备支持包 | `Keil.STM32F4xx_DFP` 3.1.1 |
| 配置工具 | STM32CubeMX 6.15.0 |
| STM32 固件包 | STM32Cube FW_F4 V1.28.3 |
| 操作系统组件 | FreeRTOS，源码位于 `App/Middlewares/` |
| ESP-01S 开发 | Arduino IDE 及 ESP8266 Arduino core，板型选择 `Generic ESP8266 Module` |

Keil 工程中记录的版本用于还原工程配置，并不代表其他版本已验证兼容。

## 硬件接线

下表依据应用程序的 GPIO、SPI、CAN 和 UART 初始化代码整理；请结合实际开发板原理图检查供电与引脚，所有模块需要共地。

### W25Q64 与 STM32

| W25Q64 信号 | STM32 引脚 | 说明 |
| --- | --- | --- |
| CS / NSS | PA4 | 软件控制片选 |
| CLK | PA5 | SPI1_SCK |
| DO / MISO | PA6 | SPI1_MISO |
| DI / MOSI | PA7 | SPI1_MOSI |
| VCC | 3.3 V | 按模块规格供电 |
| GND | GND | 共地 |

Bootloader 和应用程序均使用上述 SPI1 引脚；模块的 WP、HOLD 等引脚应按实际器件和模块原理图处理。

### CAN 总线

| STM32 引脚 | CAN 收发器端 | 说明 |
| --- | --- | --- |
| PA12 | TXD | CAN1_TX |
| PA11 | RXD | CAN1_RX |
| GND | GND | 共地 |

CAN 控制器需要通过合适的 CAN 收发器连接总线：收发器的 CANH、CANL 对应连接总线 CANH、CANL，供电及使能脚按模块规格接线。总线两端各接 120 Ω 终端电阻；不要将 STM32 的 PA11/PA12 直接接到 CANH/CANL。

### ESP-01S OTA 桥接

| ESP-01S | STM32 引脚 | 说明 |
| --- | --- | --- |
| TX / GPIO1 | PA10 | USART1_RX |
| RX / GPIO3 | PA9 | USART1_TX |
| GND | GND | 共地 |
| VCC | 3.3 V | 稳定供电，建议具备至少 500 mA 峰值能力 |

USART1 配置为 115200、8N1。ESP-01S 的 EN/CH_PD 保持高电平；GPIO0 在烧录时拉低、正常启动时保持高电平。详细测试和启动顺序见 [桥接说明](App/ESP8266Bridge/README.md)。

### OneNet 串口及其他接口

| STM32 引脚 | 用途 |
| --- | --- |
| PA2 / USART2_TX | 接支持相应 AT 指令的通信模块 RX |
| PA3 / USART2_RX | 接通信模块 TX |
| PB6 / I2C1_SCL | I²C 时钟 |
| PB7 / I2C1_SDA | I²C 数据 |
| PC13 | GPIO 输出，实际连接设备以开发板原理图为准 |

OneNet 代码通过 USART2 发送 AT 指令，串口配置为 115200、8N1。模块型号和供电接法应以实际使用的硬件为准；I²C 上拉电阻也需根据实际电路配置。

## 编译与烧录

1. 安装 Keil MDK、适用于 STM32F407 的设备支持包和工程所需的 ARM 编译器。
2. 在 Keil 中分别打开 `Bootloader/MDK-ARM/BOOTLOADER-TWO.uvprojx` 和 `App/MDK-ARM/APP_TWO.uvprojx`，选择对应目标并编译。
3. Bootloader 代码以 `0x08010000` 作为应用程序入口。烧录前请核对应用工程实际选择的 Flash 地址、链接配置及中断向量表设置，再分别烧录 Bootloader 和应用程序。
4. 如需修改外设配置，可使用对应目录中的 `.ioc` 文件打开 STM32CubeMX 工程。重新生成代码后，应检查用户代码和工程配置。

## 配置

- **OneNet**：在 `App/Drivers/user/OneNet.h` 中配置产品 ID、设备名、令牌和主题。仓库中的值是占位符。
- **ESP8266**：在 `App/ESP8266Bridge/bridge_config.h` 中配置 Wi-Fi 名称和密码，再使用 Arduino ESP8266 core 编译烧录。详细接线见桥接程序的 README。
- 请勿将真实 Wi-Fi 密码或设备令牌提交到公开仓库。

## 验证状态

发布时检查了两套 Keil 工程的源文件引用。当前环境中的 Keil 提示 ARM Toolchain 未安装，因此尚未完成编译验证；烧录、CAN 通信、OneNet 连接和 OTA 升级及回滚仍需在实际硬件上分别验证。

## 许可证

项目代码采用 [MIT License](LICENSE)。第三方组件保留其各自目录中的许可证及版权声明。
