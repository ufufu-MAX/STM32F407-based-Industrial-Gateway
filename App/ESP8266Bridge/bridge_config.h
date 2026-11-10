#pragma once

#include <Arduino.h>

static constexpr char BRIDGE_WIFI_SSID[] = "YOUR_WIFI_SSID";
static constexpr char BRIDGE_WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
static constexpr char BRIDGE_HOSTNAME[] = "stm32-ota-bridge";

static constexpr uint16_t BRIDGE_TCP_PORT = 8266U;
static constexpr uint32_t BRIDGE_UART_BAUD = 115200U;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000U;
static constexpr uint32_t BRIDGE_WRITE_STALL_TIMEOUT_MS = 1000U;
static constexpr size_t BRIDGE_BUFFER_SIZE = 256U;
