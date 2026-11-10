#ifndef _W25Q64_H__
#define _W25Q64_H__

#include "FreeRTOS.h"
#include "task.h"

#define W25Q64_FWDATA_ADD              0x00000000// OTA固件包信息
#define W25Q64_OTA_STATE_ADD           0x001D6000U
#define W25Q64_APP_VERSION_ADD         0x001D7000U

#define OTA_VERSION_SECTOR_SIZE        0x1000U
#define OTA_VERSION_RECORD_SIZE        16U
#define OTA_VERSION_RECORD_MAGIC       0x3156524FU
#define OTA_VERSION_STATE_PENDING      1U
#define OTA_VERSION_STATE_CONFIRMED    2U

#define OTA_STATE_RECORD_MAGIC         0x3154534FU
#define OTA_STATE_RECORD_SIZE          16U
#define OTA_STATE_BACKUPING            1U
#define OTA_STATE_BACKUP_READY         2U
#define OTA_STATE_INSTALLING           3U
#define OTA_STATE_PENDING_CONFIRM      4U
#define OTA_STATE_BOOTING              5U
#define OTA_STATE_CONFIRMED            6U
#define OTA_STATE_ROLLBACK             7U
#define OTA_STATE_ROLLBACK_DONE        8U

#define W25Q64_WriteEnable         0x06// 写使能
#define W25Q64_WritrDisable        0x04// 写失能
#define W25Q64_ReadState           0x05// 读取状态

#define W25Q64_ReadData            0x03// 页读取
#define W25Q64_WritePage           0x02// 页写入

#define W25Q64_SectorErase         0x20// 页擦除
#define W25Q64_PageSize            0x100// 页大小

#define SPI_DATA_BIT              (1 << 0)// UART唤醒spiTask
#define SPI_DMA_BIT              (1 << 1)// SPI写入w25q
#define SPI_DMA_ERROR_BIT         (1UL << 2)

#define SPI_DMA_NOTIFY_MASK       (SPI_DMA_BIT | SPI_DMA_ERROR_BIT)

void W25Q_Enable(void);// 使能

void W25Q_Disable(void);// 失能

uint8_t W25Q_WriteEnable(void);// 写使能

uint8_t W25Q_EraseSector(uint32_t Add);// 扇擦除

uint8_t W25Q_WritePage(uint32_t Add,uint8_t *pData,uint16_t len);// 页写入

uint8_t W25Q_ReadDataBytes(uint32_t Add,uint8_t *pData,uint16_t len);// 读取指定地址数据

uint8_t W25Q_EventBusy(uint8_t *BusyFlag);// 询问Buys状态

void W25Q_WritrBINTask(void *arg);// 写入BIN文件

void W25Q_ResetWriteAddress(void);// 新一轮OTA从镜像区起始地址写入

uint8_t W25Q_WaitReady(uint32_t TimeoutMs);// 重复检测buys是否忙

uint8_t OTA_ConfirmRunningImage(void);// 新 APP 调度正常后确认本次升级

uint8_t OTA_ReadStoredFirmwareVersion(uint32_t *Version,uint8_t *VersionWritten);// 读取已保存的固件版本

uint8_t OTA_WriteStoredFirmwareVersion(uint32_t Version);// 追加保存待确认升级版本

uint8_t OTA_ConfirmStoredFirmwareVersion(void);// 将待确认版本提升为正式版本

#endif
