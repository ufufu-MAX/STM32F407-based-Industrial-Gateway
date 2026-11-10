#ifndef _W25Q64_H__
#define _W25Q64_H__

#include <stdint.h>

#define W25Q64_WriteEnable         0x06// 写使能
#define W25Q64_WritrDisable        0x04// 写失能
#define W25Q64_ReadState           0x05// 读取状态

#define W25Q64_ReadData            0x03// 页读取
#define W25Q64_WritePage           0x02// 页写入

#define W25Q64_SectorErase         0x20// 页擦除
#define W25Q64_PageSize            0x100// 页大小

#define W25Q64_OTA_Data_ADD        0x00000000U  // W25Q64 中升级信息的起始地址
#define W25Q64_OTA_BIN_ADD         0x00001000U  // W25Q64 中 BIN 文件的起始地址
#define W25Q64_OTA_BACKUP_INFO_ADD 0x000EB000U  // 旧 APP 备份元数据扇区
#define W25Q64_OTA_BACKUP_BIN_ADD  0x000EC000U  // 旧 APP 完整备份起始地址
#define W25Q64_OTA_STATE_ADD       0x001D6000U  // 掉电安全状态日志扇区
#define W25Q64_APP_VERSION_ADD     0x001D7000U  // APP 固件版本追加日志扇区

#define OTA_INFO_LENGTH            18U          // 升级信息固定占用 18 字节
#define OTA_COPY_BUFFER_SIZE       2048U        // 每次从 W25Q64 搬运 2048 字节
#define OTA_APP_ADDRESS            0x08010000U  // STM32 内部 Flash 的 APP 起始地址
#define OTA_APP_MAX_SIZE           0x000EA000U  // APP 最大允许大小：936KB
#define OTA_FRAME_SIZE             1024U        // APP 数据帧固定有效载荷长度
#define OTA_BACKUP_SIZE            OTA_APP_MAX_SIZE
#define OTA_BACKUP_INFO_LENGTH     16U
#define OTA_BACKUP_MAGIC           0x31504B42U  // 小端字节为 "BKP1"
#define OTA_BACKUP_READY_FLAG      0xA5A55A5AU
#define OTA_STATE_RECORD_MAGIC     0x3154534FU  // 小端字节为 "OST1"
#define OTA_STATE_RECORD_SIZE      16U
#define OTA_VERSION_SECTOR_SIZE    0x1000U
#define OTA_VERSION_RECORD_SIZE    16U
#define OTA_VERSION_RECORD_MAGIC   0x3156524FU
#define OTA_VERSION_STATE_PENDING  1U
#define OTA_VERSION_STATE_CONFIRMED 2U

#define OTA_STATE_NONE             0U
#define OTA_STATE_BACKUPING        1U
#define OTA_STATE_BACKUP_READY     2U
#define OTA_STATE_INSTALLING       3U
#define OTA_STATE_PENDING_CONFIRM  4U
#define OTA_STATE_BOOTING          5U
#define OTA_STATE_CONFIRMED        6U
#define OTA_STATE_ROLLBACK         7U
#define OTA_STATE_ROLLBACK_DONE    8U

#define FW_Update_Head1  0xA5  // 升级信息第 1 个头字节
#define FW_Update_Head2  0xA5  // 升级信息第 2 个头字节
#define FW_Update_Tail1  0x5A  // 升级信息第 1 个尾字节
#define FW_Update_Tail2  0x5A  // 升级信息第 2 个尾字节

// W25Q64 中 18 字节升级信息的格式：
// [0..1]   : 0xA5 0xA5
// [2..5]   : 固件版本，小端格式
// [6..9]   : BIN 大小，小端格式
// [10..11] : 固定帧长 1024，小端格式
// [12..15] : 整个 BIN 的 STM32F4 CRC32，小端格式
// [16..17] : 0x5A 0x5A
typedef struct
{
	uint32_t bin_size;          // BIN 文件的真实字节数，用来决定搬运多少数据
	uint32_t firmware_version;  // 新固件的版本号，目前只读取和保存，不参与比较
	uint32_t image_crc32;       // 整个 BIN 的 STM32F4 CRC32
} OTA_Info_t;

typedef struct
{
	uint32_t magic;
	uint32_t image_size;
	uint32_t image_crc32;
	uint32_t ready_flag;
} OTA_BackupInfo_t;

void W25Q_Enable(void);
void W25Q_Disable(void);
uint8_t W25Q_WriteEnable(void);
uint8_t W25Q_EventBusy(void);
uint8_t W25Q_ReadData(uint8_t *pData, uint16_t len, uint32_t address);
uint8_t W25Q_EraseSector(uint32_t address);

uint8_t OTA_Data_Read(OTA_Info_t *info);
uint8_t OTA_BackupCurrentApp(void);
uint8_t OTA_BackupIsValid(void);
uint8_t OTA_RestoreBackup(void);
uint8_t OTA_CopyToFlash(const OTA_Info_t *info);
uint8_t OTA_VerifyInternalFlashCrc32(uint32_t address,
									uint32_t image_size,
									uint32_t expected_crc);
uint8_t OTA_StateRead(uint32_t *state);
uint8_t OTA_StateReset(void);
uint8_t OTA_StateAppend(uint32_t state);
uint8_t OTA_VersionCandidateAllowed(uint32_t version);
uint8_t OTA_VersionEnsurePending(uint32_t version);
uint8_t OTA_VersionConfirmPending(void);

#endif
