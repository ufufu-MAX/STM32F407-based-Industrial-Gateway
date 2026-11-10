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
