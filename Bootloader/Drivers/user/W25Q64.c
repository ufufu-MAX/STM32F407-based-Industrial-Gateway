#include "main.h"
#include "can.h"
#include "dma.h"
#include "spi.h"
#include "W25Q64.h"
#include "Flash.h"

// OTA 搬运缓冲区定义在 main.c 中。
// 每次先把 W25Q64 数据读进该缓冲区，再写入 STM32 内部 Flash。
extern uint8_t OTA_Buffer[OTA_COPY_BUFFER_SIZE];

#define W25Q_TIMEOUT_MS  5000U  // 等待 W25Q64 擦除/写入完成的最长时间：5 秒

// 把 4 个小端字节组合成一个 uint32_t 数值。
static uint32_t OTA_ReadU32LE(const uint8_t *data)
{
	return ((uint32_t)data[0]) |
		   ((uint32_t)data[1] << 8) |
		   ((uint32_t)data[2] << 16) |
		   ((uint32_t)data[3] << 24);
}

// 把 2 个小端字节组合成一个 uint16_t 数值。
static uint16_t OTA_ReadU16LE(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

// 模拟 STM32F4 默认 CRC 外设处理一个 32 位 word：
// polynomial=0x04C11DB7，init=0xFFFFFFFF，无反转，无 final xor。
static uint32_t OTA_Crc32Word(uint32_t crc, uint32_t word)
{
	uint8_t bit;

	crc ^= word;
	for (bit = 0U; bit < 32U; bit++)
	{
		if ((crc & 0x80000000U) != 0U)
		{
			crc = (crc << 1) ^ 0x04C11DB7U;
		}
		else
		{
			crc <<= 1;
		}
	}

	return crc;
}

static uint8_t W25Q_WaitReady(uint32_t timeout_ms)
{
	uint32_t start_tick = HAL_GetTick();  // 开始等待时的 HAL 毫秒计数

	while (W25Q_EventBusy() != 0U)
	{
		if ((HAL_GetTick() - start_tick) >= timeout_ms)
		{
			return 0U;
		}
	}

	return 1U;
}

void W25Q_Enable(void)
{
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_RESET);
}

void W25Q_Disable(void)
{
	HAL_GPIO_WritePin(GPIOA,GPIO_PIN_4,GPIO_PIN_SET);
}

uint8_t W25Q_WriteEnable(void)
{
	uint8_t cmd = W25Q64_WriteEnable;  // 发送给 W25Q64 的“写使能”命令 0x06
	HAL_StatusTypeDef status;          // 保存 HAL SPI 发送是否成功

	W25Q_Enable();
	status = HAL_SPI_Transmit(&hspi1, &cmd, 1U, 100U);
	W25Q_Disable();

	return (status == HAL_OK) ? 1U : 0U;
}

uint8_t W25Q_EventBusy(void)
{
	uint8_t cmd = W25Q64_ReadState;  // 读取 W25Q64 状态寄存器的命令 0x05
	uint8_t status = 1U;             // W25Q64 状态寄存器，bit0=1 表示忙

	W25Q_Enable();
	if (HAL_SPI_Transmit(&hspi1, &cmd, 1U, 100U) == HAL_OK)
	{
		(void)HAL_SPI_Receive(&hspi1, &status, 1U, 100U);
	}
	W25Q_Disable();

	return status & 0x01U;
}

uint8_t W25Q_ReadData(uint8_t *pData, uint16_t len, uint32_t address)
{
	// cmd[0] 是读命令；cmd[1..3] 是 W25Q64 的 24 位地址。
	uint8_t cmd[4];
	HAL_StatusTypeDef status;  // 保存 SPI 命令发送和数据接收结果

	if ((pData == 0) || (len == 0U))
	{
		return 0U;
	}

	if (W25Q_WaitReady(W25Q_TIMEOUT_MS) == 0U)
	{
		return 0U;
	}

	cmd[0] = W25Q64_ReadData;
	cmd[1] = (uint8_t)(address >> 16);
	cmd[2] = (uint8_t)(address >> 8);
	cmd[3] = (uint8_t)address;

	W25Q_Enable();
	status = HAL_SPI_Transmit(&hspi1, cmd, 4U, 100U);
	if (status == HAL_OK)
	{
		// Bootloader 中使用阻塞读取，返回时数据一定已经可用。
		status = HAL_SPI_Receive(&hspi1, pData, len, 1000U);
	}
	W25Q_Disable();

	return (status == HAL_OK) ? 1U : 0U;
}

uint8_t W25Q_EraseSector(uint32_t address)
{
	// cmd[0] 是扇区擦除命令；cmd[1..3] 是要擦除的 24 位地址。
	uint8_t cmd[4];
	HAL_StatusTypeDef status;  // 保存扇区擦除命令是否发送成功

	if (W25Q_WaitReady(W25Q_TIMEOUT_MS) == 0U)
	{
		return 0U;
	}

	if (W25Q_WriteEnable() == 0U)
	{
		return 0U;
	}

	cmd[0] = W25Q64_SectorErase;
	cmd[1] = (uint8_t)(address >> 16);
	cmd[2] = (uint8_t)(address >> 8);
	cmd[3] = (uint8_t)address;

	W25Q_Enable();
	status = HAL_SPI_Transmit(&hspi1, cmd, 4U, 100U);
	W25Q_Disable();

	if (status != HAL_OK)
	{
		return 0U;
	}

	return W25Q_WaitReady(W25Q_TIMEOUT_MS);
}

// 写入一页以内的数据；调用者负责保证写入范围不跨越 256 字节页边界。
static uint8_t W25Q_WritePageData(uint32_t address,
								  const uint8_t *data,
								  uint16_t length)
{
	uint8_t cmd[4];
	HAL_StatusTypeDef status;

	if ((data == 0) || (length == 0U) || (length > W25Q64_PageSize) ||
		(((address & (W25Q64_PageSize - 1U)) + length) > W25Q64_PageSize))
	{
		return 0U;
	}

	if ((W25Q_WaitReady(W25Q_TIMEOUT_MS) == 0U) ||
		(W25Q_WriteEnable() == 0U))
	{
		return 0U;
	}

	cmd[0] = W25Q64_WritePage;
	cmd[1] = (uint8_t)(address >> 16);
	cmd[2] = (uint8_t)(address >> 8);
	cmd[3] = (uint8_t)address;

	W25Q_Enable();
	status = HAL_SPI_Transmit(&hspi1, cmd, 4U, 100U);
	if (status == HAL_OK)
	{
		status = HAL_SPI_Transmit(&hspi1, (uint8_t *)data, length, 1000U);
	}
	W25Q_Disable();

	if (status != HAL_OK)
	{
		return 0U;
	}

	return W25Q_WaitReady(W25Q_TIMEOUT_MS);
}

static void OTA_WriteU32LE(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static uint32_t OTA_StateRecordCrc(uint32_t sequence, uint32_t state)
{
	uint32_t crc = 0xFFFFFFFFU;

	crc = OTA_Crc32Word(crc, OTA_STATE_RECORD_MAGIC);
	crc = OTA_Crc32Word(crc, sequence);
	crc = OTA_Crc32Word(crc, state);
	return crc;
}

// 扫描追加式状态日志。掉电产生的不完整记录会因 CRC 错误被跳过，
// previous valid record 仍然可以作为恢复依据。
static uint8_t OTA_StateScan(uint32_t *latest_state,
								 uint32_t *latest_sequence,
								 uint32_t *next_address)
{
	uint8_t record[OTA_STATE_RECORD_SIZE];
	uint32_t offset;
	uint32_t magic;
	uint32_t sequence;
	uint32_t state;
	uint32_t record_crc;
	uint8_t erased;
	uint8_t index;

	if ((latest_state == 0) || (latest_sequence == 0) || (next_address == 0))
	{
		return 0U;
	}

	*latest_state = OTA_STATE_NONE;
	*latest_sequence = 0U;
	*next_address = 0xFFFFFFFFU;

	for (offset = 0U; offset < 0x1000U; offset += OTA_STATE_RECORD_SIZE)
	{
		if (W25Q_ReadData(record,
						   OTA_STATE_RECORD_SIZE,
						   W25Q64_OTA_STATE_ADD + offset) == 0U)
		{
			return 0U;
		}

		erased = 1U;
		for (index = 0U; index < OTA_STATE_RECORD_SIZE; index++)
		{
			if (record[index] != 0xFFU)
			{
				erased = 0U;
				break;
			}
		}

		if (erased != 0U)
		{
			*next_address = W25Q64_OTA_STATE_ADD + offset;
			break;
		}

		magic = OTA_ReadU32LE(&record[0]);
		sequence = OTA_ReadU32LE(&record[4]);
		state = OTA_ReadU32LE(&record[8]);
		record_crc = OTA_ReadU32LE(&record[12]);

		if ((magic == OTA_STATE_RECORD_MAGIC) &&
			(state >= OTA_STATE_BACKUPING) &&
			(state <= OTA_STATE_ROLLBACK_DONE) &&
			(record_crc == OTA_StateRecordCrc(sequence, state)) &&
			(sequence >= *latest_sequence))
		{
			*latest_sequence = sequence;
			*latest_state = state;
		}
	}

	return 1U;
}

uint8_t OTA_StateRead(uint32_t *state)
{
	uint32_t sequence;
	uint32_t next_address;

	if (state == 0)
	{
		return 0U;
	}

	return OTA_StateScan(state, &sequence, &next_address);
}

uint8_t OTA_StateReset(void)
{
	return W25Q_EraseSector(W25Q64_OTA_STATE_ADD);
}

uint8_t OTA_StateAppend(uint32_t state)
{
	uint8_t record[OTA_STATE_RECORD_SIZE];
	uint32_t latest_state;
	uint32_t sequence;
	uint32_t next_address;
	uint32_t crc;

	if ((state < OTA_STATE_BACKUPING) ||
		(state > OTA_STATE_ROLLBACK_DONE) ||
		(OTA_StateScan(&latest_state, &sequence, &next_address) == 0U) ||
		(next_address == 0xFFFFFFFFU))
	{
		return 0U;
	}

	sequence++;
	crc = OTA_StateRecordCrc(sequence, state);
	OTA_WriteU32LE(&record[0], OTA_STATE_RECORD_MAGIC);
	OTA_WriteU32LE(&record[4], sequence);
	OTA_WriteU32LE(&record[8], state);
	OTA_WriteU32LE(&record[12], crc);

	// CRC 最后单独写入；掉电时未完成的记录不会覆盖上一条有效状态。
	if (W25Q_WritePageData(next_address, record, 12U) == 0U)
	{
		return 0U;
	}

	return W25Q_WritePageData(next_address + 12U, &record[12], 4U);
}

static uint32_t OTA_VersionRecordCrc(uint32_t version,uint32_t state)
{
	uint32_t crc = 0xFFFFFFFFU;

	crc = OTA_Crc32Word(crc,OTA_VERSION_RECORD_MAGIC);
	crc = OTA_Crc32Word(crc,version);
	crc = OTA_Crc32Word(crc,state);
	return crc;
}

static uint8_t OTA_VersionScan(uint32_t *confirmed_version,uint8_t *confirmed_valid,uint32_t *pending_version,uint8_t *pending_valid,uint32_t *next_address)
{
	uint8_t record[OTA_VERSION_RECORD_SIZE]; uint32_t offset; uint32_t version; uint32_t state; uint32_t record_crc; uint8_t erased; uint8_t index;

	if((confirmed_version == 0) || (confirmed_valid == 0) || (pending_version == 0) || (pending_valid == 0) || (next_address == 0)){
		return 0U;
	}

	*confirmed_version = 0xFFFFFFFFU;
	*confirmed_valid = 0U;
	*pending_version = 0xFFFFFFFFU;
	*pending_valid = 0U;
	*next_address = 0xFFFFFFFFU;

	for(offset = 0U; offset < OTA_VERSION_SECTOR_SIZE; offset += OTA_VERSION_RECORD_SIZE){
		if(W25Q_ReadData(record,OTA_VERSION_RECORD_SIZE,W25Q64_APP_VERSION_ADD + offset) == 0U){
			return 0U;
		}

		erased = 1U;
		for(index = 0U; index < OTA_VERSION_RECORD_SIZE; index++){
			if(record[index] != 0xFFU){
				erased = 0U;
				break;
			}
		}
		if(erased != 0U){
			*next_address = W25Q64_APP_VERSION_ADD + offset;
			return 1U;
		}

		version = OTA_ReadU32LE(&record[4]);
		state = OTA_ReadU32LE(&record[8]);
		record_crc = OTA_ReadU32LE(&record[12]);
		if((OTA_ReadU32LE(&record[0]) != OTA_VERSION_RECORD_MAGIC) || ((state != OTA_VERSION_STATE_PENDING) && (state != OTA_VERSION_STATE_CONFIRMED)) || (record_crc != OTA_VersionRecordCrc(version,state))){
			continue;
		}

		if(state == OTA_VERSION_STATE_PENDING){
			*pending_version = version;
			*pending_valid = 1U;
		}else{
			*confirmed_version = version;
			*confirmed_valid = 1U;
			*pending_valid = 0U;
		}
	}

	return 1U;
}

static uint8_t OTA_VersionAppend(uint32_t version,uint32_t state)
{
	uint8_t record[OTA_VERSION_RECORD_SIZE]; uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint32_t crc; uint8_t confirmed_valid; uint8_t pending_valid;

	if((state != OTA_VERSION_STATE_PENDING) && (state != OTA_VERSION_STATE_CONFIRMED)){
		return 0U;
	}
	if((OTA_VersionScan(&confirmed_version,&confirmed_valid,&pending_version,&pending_valid,&next_address) == 0U) || (next_address == 0xFFFFFFFFU)){
		return 0U;
	}

	crc = OTA_VersionRecordCrc(version,state);
	OTA_WriteU32LE(&record[0],OTA_VERSION_RECORD_MAGIC);
	OTA_WriteU32LE(&record[4],version);
	OTA_WriteU32LE(&record[8],state);
	OTA_WriteU32LE(&record[12],crc);
	if(W25Q_WritePageData(next_address,record,12U) == 0U){
		return 0U;
	}

	return W25Q_WritePageData(next_address + 12U,&record[12],4U);
}

uint8_t OTA_VersionCandidateAllowed(uint32_t version)
{
	uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint8_t confirmed_valid; uint8_t pending_valid;

	if(OTA_VersionScan(&confirmed_version,&confirmed_valid,&pending_version,&pending_valid,&next_address) == 0U){
		return 0U;
	}

	return ((confirmed_valid == 0U) || (version > confirmed_version)) ? 1U : 0U;
}

uint8_t OTA_VersionEnsurePending(uint32_t version)
{
	uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint8_t confirmed_valid; uint8_t pending_valid;

	if(OTA_VersionScan(&confirmed_version,&confirmed_valid,&pending_version,&pending_valid,&next_address) == 0U){
		return 0U;
	}
	if((confirmed_valid != 0U) && (version <= confirmed_version)){
		return 0U;
	}
	if((pending_valid != 0U) && (pending_version == version)){
		return 1U;
	}

	return OTA_VersionAppend(version,OTA_VERSION_STATE_PENDING);
}

uint8_t OTA_VersionConfirmPending(void)
{
	uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint8_t confirmed_valid; uint8_t pending_valid;

	if(OTA_VersionScan(&confirmed_version,&confirmed_valid,&pending_version,&pending_valid,&next_address) == 0U){
		return 0U;
	}
	if(pending_valid == 0U){
		return 1U;
	}
	if((confirmed_valid != 0U) && (pending_version <= confirmed_version)){
		return 1U;
	}

	return OTA_VersionAppend(pending_version,OTA_VERSION_STATE_CONFIRMED);
}

// 按 STM32F4/HAL 规则把一段字节继续累加到 CRC；length 必须是 4 的倍数。
static uint32_t OTA_AccumulateAlignedCrc32(uint32_t crc,
										 const uint8_t *data,
										 uint32_t length)
{
	uint32_t offset;
	uint32_t word;

	for (offset = 0U; offset < length; offset += 4U)
	{
		word = ((uint32_t)data[offset]) |
			   ((uint32_t)data[offset + 1U] << 8) |
			   ((uint32_t)data[offset + 2U] << 16) |
			   ((uint32_t)data[offset + 3U] << 24);
		crc = OTA_Crc32Word(crc, word);
	}

	return crc;
}

uint8_t OTA_VerifyInternalFlashCrc32(uint32_t address,
									uint32_t image_size,
									uint32_t expected_crc)
{
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t offset = 0U;
	uint32_t word;
	uint8_t byte_index;

	if ((image_size == 0U) || (image_size > OTA_APP_MAX_SIZE) ||
		(address < OTA_APP_ADDRESS) ||
		((address + image_size) > (OTA_APP_ADDRESS + OTA_APP_MAX_SIZE)))
	{
		return 0U;
	}

	while (offset < image_size)
	{
		word = 0U;
		for (byte_index = 0U;
			 (byte_index < 4U) && ((offset + byte_index) < image_size);
			 byte_index++)
		{
			word |= ((uint32_t)(*(__IO uint8_t *)(address + offset + byte_index))) <<
					(8U * byte_index);
		}

		crc = OTA_Crc32Word(crc, word);
		offset += 4U;
	}

	return (crc == expected_crc) ? 1U : 0U;
}

// 校验固定为 4 字节倍数的数据区域，用于旧 APP 完整备份。
static uint8_t OTA_VerifyAlignedW25QCrc32(uint32_t address,
										 uint32_t image_size,
										 uint32_t expected_crc)
{
	uint32_t remaining = image_size;
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t chunk_size;

	while (remaining > 0U)
	{
		chunk_size = (remaining > OTA_COPY_BUFFER_SIZE) ?
					 OTA_COPY_BUFFER_SIZE : remaining;
		if (W25Q_ReadData(OTA_Buffer,
						   (uint16_t)chunk_size,
						   address) == 0U)
		{
			return 0U;
		}

		crc = OTA_AccumulateAlignedCrc32(crc, OTA_Buffer, chunk_size);
		address += chunk_size;
		remaining -= chunk_size;
	}

	return (crc == expected_crc) ? 1U : 0U;
}

// 把当前内部 APP 区完整备份到 W25Q64。
// 备份元数据扇区先擦除，ready_flag 最后单独写入，避免半份备份被误认为有效。
uint8_t OTA_BackupCurrentApp(void)
{
	uint32_t source_address = OTA_APP_ADDRESS;
	uint32_t backup_address = W25Q64_OTA_BACKUP_BIN_ADD;
	uint32_t erase_address;
	uint32_t remaining = OTA_BACKUP_SIZE;
	uint32_t chunk_size;
	uint32_t page_offset;
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t index;
	uint8_t backup_info[OTA_BACKUP_INFO_LENGTH];

	// 旧 APP 向量表无效时，没有可安全备份和回滚的固件。
	if (((*(__IO uint32_t *)OTA_APP_ADDRESS < 0x20000000U) ||
		 (*(__IO uint32_t *)OTA_APP_ADDRESS > 0x20020000U)) ||
		(((*(__IO uint32_t *)OTA_APP_ADDRESS & 0x7U) != 0U)) ||
		(((*(__IO uint32_t *)(OTA_APP_ADDRESS + 4U)) & 1U) == 0U) ||
		(((*(__IO uint32_t *)(OTA_APP_ADDRESS + 4U) & ~1U) < OTA_APP_ADDRESS) ||
		 ((*(__IO uint32_t *)(OTA_APP_ADDRESS + 4U) & ~1U) >=
		  (OTA_APP_ADDRESS + OTA_APP_MAX_SIZE))))
	{
		return 0U;
	}

	// 先使旧的 ready_flag 失效；此后任何掉电都不会误用旧/半份备份。
	if (W25Q_EraseSector(W25Q64_OTA_BACKUP_INFO_ADD) == 0U)
	{
		return 0U;
	}

	// 擦除完整备份区域，共 OTA_BACKUP_SIZE / 4KB 个扇区。
	for (erase_address = W25Q64_OTA_BACKUP_BIN_ADD;
		 erase_address < (W25Q64_OTA_BACKUP_BIN_ADD + OTA_BACKUP_SIZE);
		 erase_address += 0x1000U)
	{
		if (W25Q_EraseSector(erase_address) == 0U)
		{
			return 0U;
		}
	}

	while (remaining > 0U)
	{
		chunk_size = (remaining > OTA_COPY_BUFFER_SIZE) ?
					 OTA_COPY_BUFFER_SIZE : remaining;

		for (index = 0U; index < chunk_size; index++)
		{
			OTA_Buffer[index] = *(__IO uint8_t *)(source_address + index);
		}

		crc = OTA_AccumulateAlignedCrc32(crc, OTA_Buffer, chunk_size);

		for (page_offset = 0U;
			 page_offset < chunk_size;
			 page_offset += W25Q64_PageSize)
		{
			if (W25Q_WritePageData(backup_address + page_offset,
									&OTA_Buffer[page_offset],
									W25Q64_PageSize) == 0U)
			{
				return 0U;
			}
		}

		source_address += chunk_size;
		backup_address += chunk_size;
		remaining -= chunk_size;
	}

	// 从 W25Q64 全量读回，CRC 相等才承认备份完整。
	if (OTA_VerifyAlignedW25QCrc32(W25Q64_OTA_BACKUP_BIN_ADD,
									 OTA_BACKUP_SIZE,
									 crc) == 0U)
	{
		return 0U;
	}

	OTA_WriteU32LE(&backup_info[0], OTA_BACKUP_MAGIC);
	OTA_WriteU32LE(&backup_info[4], OTA_BACKUP_SIZE);
	OTA_WriteU32LE(&backup_info[8], crc);
	OTA_WriteU32LE(&backup_info[12], OTA_BACKUP_READY_FLAG);

	// 前 12 字节先写，ready_flag 最后单独写。
	if (W25Q_WritePageData(W25Q64_OTA_BACKUP_INFO_ADD,
								backup_info,
								12U) == 0U)
	{
		return 0U;
	}

	return W25Q_WritePageData(W25Q64_OTA_BACKUP_INFO_ADD + 12U,
								 &backup_info[12],
								 4U);
}

static uint8_t OTA_ReadBackupInfo(OTA_BackupInfo_t *info)
{
	uint8_t backup_info[OTA_BACKUP_INFO_LENGTH];

	if (info == 0)
	{
		return 0U;
	}

	if (W25Q_ReadData(backup_info,
						 OTA_BACKUP_INFO_LENGTH,
						 W25Q64_OTA_BACKUP_INFO_ADD) == 0U)
	{
		return 0U;
	}

	info->magic = OTA_ReadU32LE(&backup_info[0]);
	info->image_size = OTA_ReadU32LE(&backup_info[4]);
	info->image_crc32 = OTA_ReadU32LE(&backup_info[8]);
	info->ready_flag = OTA_ReadU32LE(&backup_info[12]);

	if ((info->magic != OTA_BACKUP_MAGIC) ||
		(info->image_size != OTA_BACKUP_SIZE) ||
		(info->ready_flag != OTA_BACKUP_READY_FLAG))
	{
		return 0U;
	}

	return 1U;
}

uint8_t OTA_BackupIsValid(void)
{
	OTA_BackupInfo_t info;

	if (OTA_ReadBackupInfo(&info) == 0U)
	{
		return 0U;
	}

	return OTA_VerifyAlignedW25QCrc32(W25Q64_OTA_BACKUP_BIN_ADD,
									  info.image_size,
									  info.image_crc32);
}

uint8_t OTA_RestoreBackup(void)
{
	OTA_BackupInfo_t info;
	uint32_t w25q_address = W25Q64_OTA_BACKUP_BIN_ADD;
	uint32_t flash_address = OTA_APP_ADDRESS;
	uint32_t remaining;
	uint32_t chunk_size;

	// 擦除内部 Flash 之前，必须先确认备份的头、ready 和整包 CRC。
	if (OTA_ReadBackupInfo(&info) == 0U)
	{
		return 0U;
	}
	if (OTA_VerifyAlignedW25QCrc32(W25Q64_OTA_BACKUP_BIN_ADD,
									 info.image_size,
									 info.image_crc32) == 0U)
	{
		return 0U;
	}

	if (Flash_Erase_APP(info.image_size) == 0U)
	{
		return 0U;
	}

	remaining = info.image_size;
	while (remaining > 0U)
	{
		chunk_size = (remaining > OTA_COPY_BUFFER_SIZE) ?
					 OTA_COPY_BUFFER_SIZE : remaining;

		if (W25Q_ReadData(OTA_Buffer,
						   (uint16_t)chunk_size,
						   w25q_address) == 0U)
		{
			return 0U;
		}

		if (Flash_Write(flash_address,
						(const uint32_t *)OTA_Buffer,
						chunk_size / 4U) == 0U)
		{
			return 0U;
		}

		w25q_address += chunk_size;
		flash_address += chunk_size;
		remaining -= chunk_size;
	}

	// 恢复后再校验内部 Flash，确保回滚结果本身可启动。
	return OTA_VerifyInternalFlashCrc32(OTA_APP_ADDRESS,
										 info.image_size,
										 info.image_crc32);
}

// 在擦除内部 APP 前，重新读取并校验 W25Q64 中的整个 BIN。
static uint8_t OTA_VerifyImageCrc32(uint32_t image_size,
									uint32_t expected_crc)
{
	uint32_t address = W25Q64_OTA_BIN_ADD;
	uint32_t remaining = image_size;
	uint32_t crc = 0xFFFFFFFFU;
	uint32_t chunk_size;
	uint32_t offset;
	uint32_t word;
	uint8_t byte_index;

	while (remaining > 0U)
	{
		chunk_size = (remaining > OTA_COPY_BUFFER_SIZE) ?
					 OTA_COPY_BUFFER_SIZE : remaining;

		if (W25Q_ReadData(OTA_Buffer,
						   (uint16_t)chunk_size,
						   address) == 0U)
		{
			return 0U;
		}

		for (offset = 0U; offset < chunk_size; offset += 4U)
		{
			// 最后不足 4 字节时补 0x00，与 APP 和上位机算法一致。
			word = 0U;
			for (byte_index = 0U;
				 (byte_index < 4U) && ((offset + byte_index) < chunk_size);
				 byte_index++)
			{
				word |= ((uint32_t)OTA_Buffer[offset + byte_index]) <<
						(8U * byte_index);
			}

			crc = OTA_Crc32Word(crc, word);
		}

		address += chunk_size;
		remaining -= chunk_size;
	}

	return (crc == expected_crc) ? 1U : 0U;
}

uint8_t OTA_Data_Read(OTA_Info_t *info)
{
	// 临时保存从 W25Q64 地址 0x000000 读出的 18 字节升级信息。
	uint8_t ota_data[OTA_INFO_LENGTH];

	if (info == 0)
	{
		return 0U;
	}

	if (W25Q_ReadData(ota_data,
					   OTA_INFO_LENGTH,
					   W25Q64_OTA_Data_ADD) == 0U)
	{
		return 0U;
	}

	if ((ota_data[0] != FW_Update_Head1) ||
		(ota_data[1] != FW_Update_Head2) ||
		(ota_data[16] != FW_Update_Tail1) ||
		(ota_data[17] != FW_Update_Tail2))
	{
		return 0U;
	}

	info->bin_size = OTA_ReadU32LE(&ota_data[6]);
	info->firmware_version = OTA_ReadU32LE(&ota_data[2]);
	info->image_crc32 = OTA_ReadU32LE(&ota_data[12]);

	if ((info->bin_size < 8U) ||
		(info->bin_size > OTA_APP_MAX_SIZE) ||
		(OTA_ReadU16LE(&ota_data[10]) != OTA_FRAME_SIZE))
	{
		return 0U;
	}

	// CRC 不一致时不擦除内部 Flash，继续保留并启动旧 APP。
	if (OTA_VerifyImageCrc32(info->bin_size, info->image_crc32) == 0U)
	{
		return 0U;
	}
	if(OTA_VersionEnsurePending(info->firmware_version) == 0U)
	{
		return 0U;
	}

	return 1U;
}

uint8_t OTA_CopyToFlash(const OTA_Info_t *info)
{
	uint32_t w25q_address = W25Q64_OTA_BIN_ADD; // 当前要读取的 W25Q64 地址，从 0x1000 开始
	uint32_t flash_address = OTA_APP_ADDRESS;   // 当前要写入的内部 Flash 地址，从 0x08010000 开始
	uint32_t remaining;                         // BIN 还有多少字节没有搬运
	uint32_t chunk_size;                        // 本轮实际从 W25Q64 读取的字节数，最大 2048
	uint32_t write_size;                        // 本轮写 Flash 的字节数，必须向上补齐到 4 的倍数
	uint32_t index;                             // 给最后一包补 0xFF 时使用的循环下标
	uint32_t app_msp;                           // BIN 向量表第 1 项：APP 初始栈顶地址
	uint32_t app_reset;                         // BIN 向量表第 2 项：APP Reset_Handler 地址

	if ((info == 0) ||
		(info->bin_size < 8U) ||
		(info->bin_size > OTA_APP_MAX_SIZE))
	{
		return 0U;
	}

	// 擦内部 APP 前，至少确认 W25Q64 中存在合法的向量表。
	if (W25Q_ReadData(OTA_Buffer, 8U, W25Q64_OTA_BIN_ADD) == 0U)
	{
		return 0U;
	}

	app_msp = OTA_ReadU32LE(&OTA_Buffer[0]);
	app_reset = OTA_ReadU32LE(&OTA_Buffer[4]);

	if ((app_msp < 0x20000000U) ||
		(app_msp > 0x20020000U) ||
		((app_reset & 1U) == 0U) ||
		((app_reset & ~1U) < OTA_APP_ADDRESS) ||
		((app_reset & ~1U) >= (OTA_APP_ADDRESS + OTA_APP_MAX_SIZE)))
	{
		return 0U;
	}

	if (Flash_Erase_APP(info->bin_size) == 0U)
	{
		return 0U;
	}

	remaining = info->bin_size; // 开始时，整个 BIN 都还没有搬运。

	while (remaining > 0U)
	{
		chunk_size = (remaining > OTA_COPY_BUFFER_SIZE) ?
					 OTA_COPY_BUFFER_SIZE : remaining;

		// 最后一包不足 4 字节时用 0xFF 补齐。
		write_size = (chunk_size + 3U) & ~3U;
		for (index = chunk_size; index < write_size; index++)
		{
			OTA_Buffer[index] = 0xFFU;
		}

		if (W25Q_ReadData(OTA_Buffer,
						   (uint16_t)chunk_size,
						   w25q_address) == 0U)
		{
			return 0U;
		}

		if (Flash_Write(flash_address,
						(const uint32_t *)OTA_Buffer,
						write_size / 4U) == 0U)
		{
			return 0U;
		}

		w25q_address += chunk_size;
		flash_address += chunk_size;
		remaining -= chunk_size;
	}

	return 1U;
}
