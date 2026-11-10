# STM32 ESP-01S Web OTA

这是与 `C:\Users\nice\Desktop\stm32-ota-sender` 功能对应的网页端。它保留了固件选择、版本、UART波特率、分包大小、ACK超时、重试、CRC32、包数、起始包预览、上传进度、停止和日志功能。

网页不能直接替代远程服务器或 ESP 固件。互联网远程升级时，网页和 ESP-01S 都连接同一个 HTTPS API：

```text
网页 --HTTPS上传固件--> OTA服务器
ESP  --HTTPS查询/下载--> OTA服务器
ESP  --UART------------> STM32
STM32 --SPI------------> W25Q64
```

## 当前串口协议

网页根据当前 STM32 代码生成14字节起始包：

```text
A5 A5 [version uint32 LE] [size uint32 LE] [frame_size uint16 LE] 5A 5A
```

之后由 ESP 按 `frame_size` 直接发送固件原始数据：

- 起始包和普通数据包等待 `0x79`
- 最后一个数据包等待 `0x7A`
- ACK超时后按配置次数重发同一数据包

## 网页调用的API

远程模式：

```text
GET    /api/devices/:deviceId
POST   /api/devices/:deviceId/ota
GET    /api/devices/:deviceId/ota/status?taskId=...
DELETE /api/devices/:deviceId/ota?taskId=...
```

局域网模式：

```text
GET    /api
POST   /api/ota
GET    /api/ota/status
DELETE /api/ota
```

`POST` 使用 `multipart/form-data`，字段如下：

| 字段 | 说明 |
| --- | --- |
| `firmware` | STM32 BIN文件 |
| `firmwareVersion` | uint32十进制版本号 |
| `firmwareVersionHex` | 显示用十六进制版本号 |
| `firmwareSize` | 固件字节数 |
| `crc32` | 与Python `binascii.crc32`一致 |
| `sha256` | 固件SHA-256 |
| `frameSize` | 256/512/1024/2048/4096 |
| `baudRate` | ESP与STM32的UART波特率 |
| `ackTimeoutMs` | ACK超时毫秒数 |
| `maxRetries` | 单包最大重试次数 |
| `startPacketHex` | 14字节起始包预览 |

创建设备任务成功时，服务器返回：

```json
{
  "taskId": "ota-20260713-001",
  "stage": "queued",
  "progress": 0,
  "message": "固件上传完成，等待设备下载"
}
```

状态接口返回：

```json
{
  "taskId": "ota-20260713-001",
  "stage": "transferring",
  "progress": 42.5,
  "sentBytes": 222822,
  "totalBytes": 524288,
  "message": "UART 218/512，等待0x79"
}
```

支持的 `stage`：`queued`、`uploading`、`downloading`、`preparing`、`transferring`、`verifying`、`installing`、`rebooting`、`completed`、`failed`、`cancelled`。

## ESP-01S使用的HTTPS接口

ESP不直接连接网页。网页和ESP分别连接同一个服务器，服务器保存固件和任务状态。ESP定时请求：

```text
GET /api/esp/devices/:deviceId/update
Authorization: Bearer <device-token>
```

有任务时返回：

```json
{
  "taskId": "ota-20260713-001",
  "version": 1,
  "size": 524288,
  "crc32": "0x12345678",
  "sha256": "...",
  "frameSize": 1024,
  "baudRate": 115200,
  "ackTimeoutMs": 3000,
  "maxRetries": 5,
  "downloadUrl": "https://ota.example.com/api/esp/firmware/ota-20260713-001"
}
```

ESP通过 `downloadUrl` 分块读取BIN，每读一个 `frameSize` 就发送到STM32并等待ACK。服务器应支持HTTP `Range`，使ESP断线后可以从STM32已确认的偏移继续下载。

ESP上报进度：

```text
POST /api/esp/devices/:deviceId/ota/:taskId/status
```

请求体示例：

```json
{
  "stage": "transferring",
  "progress": 42.5,
  "sentBytes": 222822,
  "message": "UART 218/512，等待0x79"
}
```

网页的状态接口读取这份状态，因此网页关闭不会中断升级。

## 本地预览

可以直接打开 `index.html` 选择固件并检查CRC和起始包。若要调用API，建议由本地HTTP服务器提供网页，服务端还需要允许网页来源的CORS请求。
