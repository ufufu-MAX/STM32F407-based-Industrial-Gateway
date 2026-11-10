# ESP-01S TCP-UART bridge

This firmware is the second-stage LAN OTA transport. It does not parse HTTP,
STM32 OTA frames, CRC values, sequence numbers, or ACK values. It forwards
bytes in both directions between one TCP client and STM32 USART1.

```text
PC TCP client <-> ESP8266 TCP port 8266 <-> ESP UART0 <-> STM32 USART1
```

## 1. Configure and flash ESP-01S

Edit `bridge_config.h`:

```cpp
static constexpr char BRIDGE_WIFI_SSID[] = "your-2.4G-WiFi";
static constexpr char BRIDGE_WIFI_PASSWORD[] = "your-password";
```

Use the Arduino ESP8266 core and select `Generic ESP8266 Module`. ESP-01S
normally uses 1 MB flash. Keep UART at 115200 baud.

Do not add `Serial.print()` diagnostics. UART0 is the OTA data channel and any
debug text would corrupt STM32 frames.

## 2. Wiring

| ESP-01S | STM32F407 | Purpose |
| --- | --- | --- |
| TX / GPIO1 | PA10 / USART1_RX | ESP to STM32 |
| RX / GPIO3 | PA9 / USART1_TX | STM32 ACK to ESP |
| GND | GND | Common ground |

Use a stable 3.3 V supply capable of at least 500 mA peak current. Keep EN/CH_PD
high. GPIO0 is low only while flashing the ESP and high during normal boot.

The ESP boot ROM writes diagnostic bytes on UART0. Power and boot the ESP first,
then reset STM32 before running the bridge probe.

## 3. Find the ESP address

After it joins Wi-Fi, find DHCP hostname `stm32-ota-bridge` in the router client
list. The bridge listens on TCP port `8266` and accepts one client at a time.

## 4. Safe round-trip acceptance test

Reset STM32, then run:

```powershell
python App/ESP8266Bridge/tools/tcp_bridge_probe.py --host 192.168.1.88
```

Replace the address with the ESP address. The probe sends two deliberately bad
18-byte start frames. Expected output:

```text
round 1: response 0xE1
round 2: response 0xE1
PASS: TCP-UART round trip is stable; OTA was not started
```

Two matching responses prove stable byte boundaries in both directions. This
test does not write firmware or enter the normal OTA data phase.

## Scope of this stage

- Implemented: one-client TCP server, TCP-to-UART forwarding, UART-to-TCP
  forwarding, reconnect handling, bounded 256-byte buffer, and no UART logging.
- Not yet implemented: HTTP website/API, TCP OTA sender integration, encryption,
  authentication, or hardware acceptance evidence.
