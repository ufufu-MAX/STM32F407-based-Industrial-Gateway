#include <ESP8266WiFi.h>

#include "bridge_config.h"

static WiFiServer bridgeServer(BRIDGE_TCP_PORT);
static WiFiClient bridgeClient;
static uint8_t bridgeBuffer[BRIDGE_BUFFER_SIZE];
static bool serverStarted = false;
static uint32_t lastWifiRetryMs = 0U;

static void discardUartInput()
{
  while (Serial.available() > 0) {
    (void)Serial.read();
  }
}

static void stopBridgeClient()
{
  bridgeClient.stop();
}

static void stopBridgeServer()
{
  if (!serverStarted) {
    return;
  }

  bridgeServer.stop();
  serverStarted = false;
}

static void startBridgeServer()
{
  if (serverStarted) {
    return;
  }

  bridgeServer.begin();
  bridgeServer.setNoDelay(true);
  serverStarted = true;
}

static void maintainWifi()
{
  if (WiFi.status() == WL_CONNECTED) {
    startBridgeServer();
    return;
  }

  stopBridgeClient();
  stopBridgeServer();

  const uint32_t now = millis();
  if ((uint32_t)(now - lastWifiRetryMs) < WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  lastWifiRetryMs = now;
  WiFi.disconnect();
  WiFi.begin(BRIDGE_WIFI_SSID, BRIDGE_WIFI_PASSWORD);
}

static void acceptBridgeClient()
{
  if (!serverStarted) {
    return;
  }

  WiFiClient candidate = bridgeServer.accept();
  if (!candidate) {
    return;
  }

  if (bridgeClient && bridgeClient.connected()) {
    candidate.stop();
    return;
  }

  stopBridgeClient();
  discardUartInput();
  bridgeClient = candidate;
  bridgeClient.setNoDelay(true);
}

static void forwardTcpToUart()
{
  if (!bridgeClient || !bridgeClient.connected()) {
    return;
  }

  const int tcpAvailable = bridgeClient.available();
  const int uartWritable = Serial.availableForWrite();
  if ((tcpAvailable <= 0) || (uartWritable <= 0)) {
    return;
  }

  size_t transferSize = (size_t)tcpAvailable;
  if (transferSize > (size_t)uartWritable) {
    transferSize = (size_t)uartWritable;
  }
  if (transferSize > sizeof(bridgeBuffer)) {
    transferSize = sizeof(bridgeBuffer);
  }

  const int received = bridgeClient.read(bridgeBuffer, transferSize);
  if (received <= 0) {
    return;
  }

  size_t offset = 0U;
  uint32_t stallStartedAt = millis();
  while (offset < (size_t)received) {
    const size_t written = Serial.write(&bridgeBuffer[offset],
                                        (size_t)received - offset);
    if (written == 0U) {
      if ((uint32_t)(millis() - stallStartedAt) >=
          BRIDGE_WRITE_STALL_TIMEOUT_MS) {
        stopBridgeClient();
        return;
      }
      yield();
      continue;
    }
    offset += written;
    stallStartedAt = millis();
  }
}

static void forwardUartToTcp()
{
  if (!bridgeClient || !bridgeClient.connected()) {
    discardUartInput();
    return;
  }

  const int uartAvailable = Serial.available();
  if (uartAvailable <= 0) {
    return;
  }

  size_t transferSize = (size_t)uartAvailable;
  if (transferSize > sizeof(bridgeBuffer)) {
    transferSize = sizeof(bridgeBuffer);
  }

  const size_t received = Serial.readBytes(bridgeBuffer, transferSize);
  size_t offset = 0U;
  uint32_t stallStartedAt = millis();
  while ((offset < received) && bridgeClient.connected()) {
    const size_t written = bridgeClient.write(&bridgeBuffer[offset],
                                              received - offset);
    if (written == 0U) {
      if ((uint32_t)(millis() - stallStartedAt) >=
          BRIDGE_WRITE_STALL_TIMEOUT_MS) {
        stopBridgeClient();
        return;
      }
      yield();
      continue;
    }
    offset += written;
    stallStartedAt = millis();
  }
}

void setup()
{
  Serial.begin(BRIDGE_UART_BAUD, SERIAL_8N1);
  Serial.setTimeout(0U);
  discardUartInput();

  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(BRIDGE_HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.begin(BRIDGE_WIFI_SSID, BRIDGE_WIFI_PASSWORD);
  lastWifiRetryMs = millis();
}

void loop()
{
  maintainWifi();
  acceptBridgeClient();

  if (!bridgeClient.connected()) {
    stopBridgeClient();
  }

  forwardTcpToUart();
  forwardUartToTcp();
  yield();
}
