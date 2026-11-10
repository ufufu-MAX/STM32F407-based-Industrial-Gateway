#ifndef _UART_H__
#define _UART_H__

#include "FreeRTOS.h"
#include "task.h"

#define RING_BUFFER_CAPACITY       2048U
#define RING_BUFFER_SIZE           (RING_BUFFER_CAPACITY + 1U)

#define OTA_DATA_PAYLOAD_SIZE      1024U
#define OTA_DATA_FRAME_OVERHEAD    12U
#define OTA_DATA_FRAME_MAX_SIZE    (OTA_DATA_PAYLOAD_SIZE + OTA_DATA_FRAME_OVERHEAD)

#define Uart_buffer                OTA_DATA_FRAME_MAX_SIZE
#define OTA_START_FRAME_SIZE       18U
#define Uart_FW_buffer             OTA_START_FRAME_SIZE

#define FRAME_SIZE                 OTA_DATA_PAYLOAD_SIZE

#define OTA_MAX_FIRMWARE_SIZE   0x000EA000UL // 936
#define OTA_DATA_TIMEOUT_MS     10000U
#define OTA_STORAGE_TIMEOUT_MS  5000U
#define OTA_FINALIZE_TIMEOUT_MS 30000U
#define OTA_HEADER_RETRY_MS     20U

#define OTA_DATA_HEADER_SIZE    6U
#define OTA_DATA_TRAILER_SIZE   6U

#define OTA_DATA_HEAD1          0xA5U
#define OTA_DATA_HEAD2          0x5AU
#define OTA_DATA_TAIL1          0x5AU
#define OTA_DATA_TAIL2          0xA5U

#define Uart_Head1 0x55
#define Uart_Head2 0xAA
#define Uart_Tail1 0xA5
#define Uart_Tail2 0x5A

#define OTA_RX_STAGE_HEADER        0U
#define OTA_RX_STAGE_BODY          1U

#define FW_Update_Head1  0xA5
#define FW_Update_Head2  0xA5
#define FW_Update_Tail1  0x5A
#define FW_Update_Tail2  0x5A

#define FW_Update_ACK      0x79
#define FW_Update_ACK_END  0x7A
#define FW_Update_ENDACK 0x7A

extern volatile uint32_t ota_session_id;// 当前OTA对话ID
extern volatile uint32_t ota_storage_request_session_id;// 本次要求W25Q写入的数据属于哪个会话
extern volatile uint32_t ota_storage_event_session_id;// W25发回的完成或错误属于哪个会话

#define OTA_UART_NOTIFY_MASK        (UART_DATA_BIT | UART_ERROR_BIT | OTA_STORAGE_DONE_BIT | OTA_STORAGE_ERROR_BIT | OTA_STORAGE_CRC_ERROR_BIT)// 统一通知位掩码

#define UART_DATA_BIT              (1UL << 0)// UART DMA接收完成
#define UART_ERROR_BIT             (1UL << 1)// UART/DMA错误
#define OTA_STORAGE_DONE_BIT       (1UL << 2)// W25Q写入完成
#define OTA_STORAGE_ERROR_BIT       (1UL << 3)// 等待BUSY超时
#define OTA_STORAGE_CRC_ERROR_BIT   (1UL << 4)// W25Q整固件CRC32不匹配

#define OTA_STATE_IDLE             0U// OTA空闲状态
#define OTA_STATE_RECEIVING        1U// OTA接收状态
#define OTA_STATE_FINALIZING       2U// OTA接收完成

#define OTA_INFO_ADDRESS      0x00000000U
#define OTA_IMAGE_ADDRESS     0x00001000U

#define OTA_INFO_MAGIC        0x3141544FU
#define OTA_READY_FLAG        0xA5A55A5AU
// **********************错误码***************************
#define OTA_STATUS_FRAME_HEAD_TAIL_ERR         0xE1// 帧头帧尾校验错误
#define OTA_STATUS_FW_SIZE_ERR                 0xE2// 固件包大小错误
#define OTA_STATUS_FRAME_SIZE_ERR              0xE3// 包大小错误
#define OTA_STATUS_BUSY                        0xE4// OTA忙
#define OTA_STATUS_UART_DMA_ERR                0xE5
#define OTA_STATUS_BUFFER_FULL                 0xE6
#define OTA_STATUS_FLASH_ERR                   0xE7// W25Q读取BUSY忙超时
#define OTA_STATUS_DATA_FRAME_ERR              0xE8
#define OTA_STATUS_SEQUENCE_ERR                0xE9
#define OTA_STATUS_DATA_CRC_ERR                0xEA
#define OTA_STATUS_VERSION_ERR                 0xEB// 目标固件版本不高于W25Q64中已存版本
// ******************************************************

#define OTA_RETRY             0xFF



typedef __packed struct
{
    uint32_t magic;               // OTA 信息标识 
    uint32_t image_size;          // BIN 文件大小 
    uint32_t firmware_version;    // 固件版本 
    uint32_t ready_flag;          // 升级标志，最后写入 

} OTA_Info_t;

typedef struct {
	uint8_t data[RING_BUFFER_SIZE];
	uint16_t Write_Index;
	uint16_t Read_Index;	
}s_Ringbuffer;// 环形缓冲区

typedef struct {
    uint32_t pkt_size;          // 固件总大小
    uint16_t fram_size;         // 每包有效载荷大小，固定1024
    uint32_t mcu_fw_ver;        // 固件版本号
    uint32_t image_crc32;       // 整个BIN文件的STM32F4 CRC32
    uint16_t packet_cnt;        // 固件总包数
    uint16_t end_pkt_size;      // 最后一包有效载荷大小
}s_pke;

void Ring_Buffer_Init(void);

uint16_t Get_Ring_Buffer_Lenth(void);// 获取未处理数据

uint16_t Get_Ring_Buffer_Remain(void);// 获取可写入大小

uint8_t Ring_Buffer_Write(uint8_t *rx_data,uint16_t Length);// 写入缓冲区数据

uint8_t Ring_Buffer_Read(uint8_t *read_data,uint16_t Length);// 读取缓冲区数据

void OTA_Receive_Task(void *arg);// 接收OTA数据

uint8_t ESP8266_SendCommand(uint8_t Cmd,uint16_t Max_Time);// 发送命令

uint8_t OTA_StartHeaderReceive(void);// 重新等待18字节起始包

void OTA_UART_RxCpltFromISR(BaseType_t *HigherPriorityTaskWoken);// 数据帧DMA分段接收

void OTA_NotifyStorageDone(void);// W25Q任务通知当前数据包已落盘

#endif
