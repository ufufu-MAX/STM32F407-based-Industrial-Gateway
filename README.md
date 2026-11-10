# STM32F407 Industrial Gateway

STM32F407 firmware with a FreeRTOS application, CAN handling, W25Q64-based OTA update storage, and a separate bootloader. The repository also contains an ESP8266 TCP-to-UART bridge and a web OTA interface.

## Repository layout

- `App/`: application firmware, STM32CubeMX configuration, and Keil project.
- `Bootloader/`: bootloader firmware, STM32CubeMX configuration, and Keil project.
- `App/ESP8266Bridge/`: ESP-01S TCP-to-UART bridge and probe.
- `App/WebOTA/`: web OTA interface; see its README for API requirements.

## Build and configuration

Open `Bootloader/MDK-ARM/BOOTLOADER-TWO.uvprojx` or `App/MDK-ARM/APP_TWO.uvprojx` in Keil MDK. The `.ioc` files are provided for STM32CubeMX. The bootloader expects the application at `0x08010000`; verify the selected Keil target's flash layout and vector table settings before programming a board.

For OneNet, configure the placeholder device values in `App/Drivers/user/OneNet.h`. For the ESP8266 bridge, configure `App/ESP8266Bridge/bridge_config.h` and follow its README. Do not commit real credentials. The bridge is a LAN TCP-to-UART transport; consult the component README for its limitations.

Building the projects does not verify hardware wiring, network connectivity, or OTA recovery. Test those separately on the target hardware.

## License

Project code is distributed under the MIT license in `LICENSE`. Third-party components retain their own licenses and notices in their respective directories.
