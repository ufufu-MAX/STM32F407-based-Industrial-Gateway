#include "FreeRTOS.h"
#include "main.h"   
#include "spi.h"   
#include "usart.h"
#include "string.h" 
#include "uart.h"
#include "W25Q64.h"

extern TaskHandle_t SPI_TaskHandle;
extern TaskHandle_t Uart_TaskHandle;
extern volatile uint8_t ota_last_packet;
extern uint8_t uart_data[Uart_FW_buffer];// 指令接收数组
extern CRC_HandleTypeDef hcrc;
extern s_pke packet;
uint32_t W25Q64Add = 0x00001000;
extern volatile uint8_t ota_state;

void W25Q_ResetWriteAddress(void)
{
	W25Q64Add = OTA_IMAGE_ADDRESS;
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
	uint8_t cmd = W25Q64_WriteEnable;
	W25Q_Enable();
	if(HAL_SPI_Transmit(&hspi1,&cmd,1,10) != HAL_OK){
		W25Q_Disable();
		return 0;
	}
	W25Q_Disable();
	return 1;
}

uint8_t W25Q_EventBusy(uint8_t *BusyFlag)
{
	uint8_t cmd = W25Q64_ReadState;
	if(BusyFlag == NULL){
		return 0;
	}
	*BusyFlag = 1U;
	W25Q_Enable();
	if(HAL_SPI_Transmit(&hspi1,&cmd,1,10) != HAL_OK){
		W25Q_Disable();
		return 0;
	}
	if(HAL_SPI_Receive(&hspi1,BusyFlag,1,10) != HAL_OK){
		W25Q_Disable();
		return 0;
	}
	W25Q_Disable();
	return 1;
}

uint8_t W25Q_EraseSector(uint32_t Add)
{
	uint8_t cmd[4];
	cmd[0] = W25Q64_SectorErase;
	cmd[1] = (Add >> 16) & 0xFF;
	cmd[2] = (Add >> 8) & 0xFF;
	cmd[3] = Add & 0xFF;
	if(W25Q_WriteEnable() == 0){
		return 0;
	}
	W25Q_Enable();
	if(HAL_SPI_Transmit(&hspi1,cmd,4,10) != HAL_OK){
		W25Q_Disable();
		return 0;
	}	
	W25Q_Disable();
	return 1;
}

uint8_t W25Q_WritePage(uint32_t Add,uint8_t *pData,uint16_t len)
{
	uint8_t cmd[4];
	uint32_t SPI_NotifedValue = 0;
	BaseType_t NotifyResult;
	cmd[0] = W25Q64_WritePage;
	cmd[1] = (Add >> 16) & 0xFF;
	cmd[2] = (Add >> 8) & 0xFF;
	cmd[3] = Add & 0xFF;
	if(W25Q_WriteEnable() != 1){
		return 0;
	}	
	W25Q_Enable();
	if(HAL_SPI_Transmit(&hspi1,cmd,4,10) != HAL_OK){
		W25Q_Disable();
		return 0;
	}	
	
	ulTaskNotifyValueClear(NULL,SPI_DMA_NOTIFY_MASK);
	xTaskNotifyStateClear(NULL);
	
	if(HAL_SPI_Transmit_DMA(&hspi1,pData,len) != HAL_OK){
		W25Q_Disable();
		return 0;
	}
	
	SPI_NotifedValue = 0;
	NotifyResult = xTaskNotifyWait(0,SPI_DMA_NOTIFY_MASK,&SPI_NotifedValue,pdMS_TO_TICKS(OTA_STORAGE_TIMEOUT_MS));// 等待启动的SPI_DMA是否完成
	
	if((NotifyResult != pdPASS) || (SPI_NotifedValue & SPI_DMA_BIT) == 0 || (SPI_NotifedValue & SPI_DMA_ERROR_BIT) != 0){
		HAL_SPI_Abort(&hspi1);
		W25Q_Disable();
		ulTaskNotifyValueClear(NULL,SPI_DMA_NOTIFY_MASK);
		xTaskNotifyStateClear(NULL);
		return 0;
	}	
	W25Q_Disable();
	return 1;
}

uint8_t W25Q_ReadDataBytes(uint32_t Add,uint8_t *pData,uint16_t len)
{
	uint8_t cmd[4];

	if((pData == NULL) || (len == 0U)){
		return 0U;
	}

	cmd[0] = W25Q64_ReadData;
	cmd[1] = (uint8_t)((Add >> 16) & 0xFFU);
	cmd[2] = (uint8_t)((Add >> 8) & 0xFFU);
	cmd[3] = (uint8_t)(Add & 0xFFU);

	W25Q_Enable();
	if(HAL_SPI_Transmit(&hspi1, cmd, sizeof(cmd), 10U) != HAL_OK){
		W25Q_Disable();
		return 0U;
	}

	if(HAL_SPI_Receive(&hspi1, pData, len, OTA_STORAGE_TIMEOUT_MS) != HAL_OK){
		W25Q_Disable();
		return 0U;
	}

	W25Q_Disable();
	return 1U;
}

static uint32_t OTA_StateReadU32LE(const uint8_t *data)
{
	return ((uint32_t)data[0]) |
		   ((uint32_t)data[1] << 8) |
		   ((uint32_t)data[2] << 16) |
		   ((uint32_t)data[3] << 24);
}

static void OTA_StateWriteU32LE(uint8_t *data, uint32_t value)
{
	data[0] = (uint8_t)value;
	data[1] = (uint8_t)(value >> 8);
	data[2] = (uint8_t)(value >> 16);
	data[3] = (uint8_t)(value >> 24);
}

static uint32_t OTA_StateCrcWord(uint32_t crc, uint32_t word)
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

static uint32_t OTA_StateRecordCrc(uint32_t sequence, uint32_t state)
{
	uint32_t crc = 0xFFFFFFFFU;

	crc = OTA_StateCrcWord(crc, OTA_STATE_RECORD_MAGIC);
	crc = OTA_StateCrcWord(crc, sequence);
	crc = OTA_StateCrcWord(crc, state);
	return crc;
}

static uint8_t W25Q_WriteStateBlocking(uint32_t address,
									  const uint8_t *data,
									  uint16_t length)
{
	uint8_t cmd[4];
	HAL_StatusTypeDef status;

	if ((data == NULL) || (length == 0U) ||
		(((address & 0xFFU) + length) > W25Q64_PageSize))
	{
		return 0U;
	}

	if ((W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0U) ||
		(W25Q_WriteEnable() == 0U))
	{
		return 0U;
	}

	cmd[0] = W25Q64_WritePage;
	cmd[1] = (uint8_t)(address >> 16);
	cmd[2] = (uint8_t)(address >> 8);
	cmd[3] = (uint8_t)address;

	W25Q_Enable();
	status = HAL_SPI_Transmit(&hspi1, cmd, sizeof(cmd), 100U);
	if (status == HAL_OK)
	{
		status = HAL_SPI_Transmit(&hspi1, (uint8_t *)data, length, 100U);
	}
	W25Q_Disable();

	if (status != HAL_OK)
	{
		return 0U;
	}

	return W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS);
}

static uint32_t OTA_VersionRecordCrc(uint32_t version,uint32_t state)
{
	uint32_t crc = 0xFFFFFFFFU;

	crc = OTA_StateCrcWord(crc,OTA_VERSION_RECORD_MAGIC);
	crc = OTA_StateCrcWord(crc,version);
	crc = OTA_StateCrcWord(crc,state);
	return crc;
}

static uint8_t OTA_ScanStoredFirmwareVersion(uint32_t *ConfirmedVersion,uint8_t *ConfirmedWritten,uint32_t *PendingVersion,uint8_t *PendingWritten,uint32_t *NextAddress)
{
	uint8_t record[OTA_VERSION_RECORD_SIZE]; uint32_t offset; uint32_t version; uint32_t state; uint32_t record_crc; uint8_t erased; uint8_t index;

	if((ConfirmedVersion == NULL) || (ConfirmedWritten == NULL) || (PendingVersion == NULL) || (PendingWritten == NULL) || (NextAddress == NULL)){
		return 0U;
	}

	*ConfirmedVersion = 0xFFFFFFFFUL;
	*ConfirmedWritten = 0U;
	*PendingVersion = 0xFFFFFFFFUL;
	*PendingWritten = 0U;
	*NextAddress = 0xFFFFFFFFUL;

	for(offset = 0U; offset < OTA_VERSION_SECTOR_SIZE; offset += OTA_VERSION_RECORD_SIZE){
		if(W25Q_ReadDataBytes(W25Q64_APP_VERSION_ADD + offset,record,OTA_VERSION_RECORD_SIZE) == 0U){
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
			*NextAddress = W25Q64_APP_VERSION_ADD + offset;
			return 1U;
		}

		version = OTA_StateReadU32LE(&record[4]);
		state = OTA_StateReadU32LE(&record[8]);
		record_crc = OTA_StateReadU32LE(&record[12]);
		if((OTA_StateReadU32LE(&record[0]) != OTA_VERSION_RECORD_MAGIC) || ((state != OTA_VERSION_STATE_PENDING) && (state != OTA_VERSION_STATE_CONFIRMED)) || (record_crc != OTA_VersionRecordCrc(version,state))){
			continue;
		}

		if(state == OTA_VERSION_STATE_PENDING){
			*PendingVersion = version;
			*PendingWritten = 1U;
		}else{
			*ConfirmedVersion = version;
			*ConfirmedWritten = 1U;
			*PendingWritten = 0U;
		}
	}

	return 1U;
}

static uint8_t OTA_AppendVersionRecord(uint32_t version,uint32_t state)
{
	uint8_t record[OTA_VERSION_RECORD_SIZE]; uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint32_t record_crc; uint8_t confirmed_written; uint8_t pending_written;

	if((state != OTA_VERSION_STATE_PENDING) && (state != OTA_VERSION_STATE_CONFIRMED)){
		return 0U;
	}
	if(OTA_ScanStoredFirmwareVersion(&confirmed_version,&confirmed_written,&pending_version,&pending_written,&next_address) == 0U){
		return 0U;
	}

	if(next_address == 0xFFFFFFFFUL){
		return 0U;
	}

	record_crc = OTA_VersionRecordCrc(version,state);
	OTA_StateWriteU32LE(&record[0],OTA_VERSION_RECORD_MAGIC);
	OTA_StateWriteU32LE(&record[4],version);
	OTA_StateWriteU32LE(&record[8],state);
	OTA_StateWriteU32LE(&record[12],record_crc);
	return W25Q_WriteStateBlocking(next_address,record,OTA_VERSION_RECORD_SIZE);
}

uint8_t OTA_ReadStoredFirmwareVersion(uint32_t *Version,uint8_t *VersionWritten)
{
	uint32_t pending_version; uint32_t next_address; uint8_t pending_written;

	return OTA_ScanStoredFirmwareVersion(Version,VersionWritten,&pending_version,&pending_written,&next_address);
}

uint8_t OTA_WriteStoredFirmwareVersion(uint32_t Version)
{
	uint32_t stored_version; uint32_t pending_version; uint32_t next_address; uint8_t version_written; uint8_t pending_written;

	if(OTA_ScanStoredFirmwareVersion(&stored_version,&version_written,&pending_version,&pending_written,&next_address) == 0U){
		return 0U;
	}
	if((version_written != 0U) && (Version <= stored_version)){
		return 0U;
	}

	return OTA_AppendVersionRecord(Version,OTA_VERSION_STATE_PENDING);
}

uint8_t OTA_ConfirmStoredFirmwareVersion(void)
{
	uint32_t confirmed_version; uint32_t pending_version; uint32_t next_address; uint8_t confirmed_written; uint8_t pending_written;

	if(OTA_ScanStoredFirmwareVersion(&confirmed_version,&confirmed_written,&pending_version,&pending_written,&next_address) == 0U){
		return 0U;
	}
	if(pending_written == 0U){
		return 1U;
	}
	if((confirmed_written != 0U) && (pending_version <= confirmed_version)){
		return 1U;
	}

	return OTA_AppendVersionRecord(pending_version,OTA_VERSION_STATE_CONFIRMED);
}

static uint8_t OTA_StateScanForApp(uint32_t *latest_state,
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

	if ((latest_state == NULL) || (latest_sequence == NULL) ||
		(next_address == NULL))
	{
		return 0U;
	}

	*latest_state = 0U;
	*latest_sequence = 0U;
	*next_address = 0xFFFFFFFFU;

	for (offset = 0U; offset < 0x1000U; offset += OTA_STATE_RECORD_SIZE)
	{
		if (W25Q_ReadDataBytes(W25Q64_OTA_STATE_ADD + offset,
									record,
									OTA_STATE_RECORD_SIZE) == 0U)
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

		magic = OTA_StateReadU32LE(&record[0]);
		sequence = OTA_StateReadU32LE(&record[4]);
		state = OTA_StateReadU32LE(&record[8]);
		record_crc = OTA_StateReadU32LE(&record[12]);

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

uint8_t OTA_ConfirmRunningImage(void)
{
	uint8_t record[OTA_STATE_RECORD_SIZE];
	uint32_t latest_state;
	uint32_t sequence;
	uint32_t next_address;
	uint32_t crc;

	if (OTA_StateScanForApp(&latest_state, &sequence, &next_address) == 0U)
	{
		return 0U;
	}

	// 普通启动不需要写 W25Q；BOOTING 或已确认但版本尚未提升时需要处理。
	if ((latest_state != OTA_STATE_BOOTING) && (latest_state != OTA_STATE_CONFIRMED))
	{
		return 1U;
	}
	if (latest_state == OTA_STATE_CONFIRMED)
	{
		return OTA_ConfirmStoredFirmwareVersion();
	}
	if (next_address == 0xFFFFFFFFU)
	{
		return 0U;
	}

	sequence++;
	crc = OTA_StateRecordCrc(sequence, OTA_STATE_CONFIRMED);
	OTA_StateWriteU32LE(&record[0], OTA_STATE_RECORD_MAGIC);
	OTA_StateWriteU32LE(&record[4], sequence);
	OTA_StateWriteU32LE(&record[8], OTA_STATE_CONFIRMED);
	OTA_StateWriteU32LE(&record[12], crc);

	if (W25Q_WriteStateBlocking(next_address, record, 12U) == 0U)
	{
		return 0U;
	}

	if(W25Q_WriteStateBlocking(next_address + 12U, &record[12], 4U) == 0U){
		return 0U;
	}

	return OTA_ConfirmStoredFirmwareVersion();
}

static uint8_t W25Q_CalculateImageCrc32(uint32_t Add,
										 uint32_t image_size,
										 uint32_t *image_crc32)
{
	static uint32_t crc_words[W25Q64_PageSize / sizeof(uint32_t)];
	uint32_t remaining;
	uint32_t crc = 0xFFFFFFFFU;
	uint16_t read_len;
	uint32_t word_count;
	uint8_t first_block = 1U;

	if((image_crc32 == NULL) || (image_size == 0U)){
		return 0U;
	}

	remaining = image_size;
	while(remaining > 0U){
		read_len = (remaining >= W25Q64_PageSize) ?
					 W25Q64_PageSize : (uint16_t)remaining;

		memset(crc_words, 0, sizeof(crc_words));
		if(W25Q_ReadDataBytes(Add,
								  (uint8_t *)crc_words,
								  read_len) == 0U){
			return 0U;
		}

		word_count = ((uint32_t)read_len + 3U) / 4U;
		if(first_block != 0U){
			crc = HAL_CRC_Calculate(&hcrc, crc_words, word_count);
			first_block = 0U;
		}else{
			crc = HAL_CRC_Accumulate(&hcrc, crc_words, word_count);
		}

		Add += read_len;
		remaining -= read_len;
	}

	*image_crc32 = crc;
	return 1U;
}


static uint8_t W25Q_IsSessionCurrent(uint32_t session_id)
{
    if(session_id != ota_session_id)
    {
        return 0U;
    }

    if((ota_state != OTA_STATE_RECEIVING) && (ota_state != OTA_STATE_FINALIZING))
    {
        return 0U;
    }

    return 1U;
}

static void W25Q_ReportstorageDone(uint32_t session_id)// 上报存储成功
{
    if(W25Q_IsSessionCurrent(session_id) == 0U)
    {
        return;
    }

    ota_storage_event_session_id = session_id;
    __DMB();

    (void)xTaskNotify(Uart_TaskHandle,
                      OTA_STORAGE_DONE_BIT,
                      eSetBits);
}

static void W25Q_ReportstorageError(uint32_t session_id)// 上报存储错误
{
	
	W25Q_Disable();
	if(W25Q_IsSessionCurrent(session_id) == 0){
		return;
	}	
	ota_storage_event_session_id = session_id;
	__DMB();
	xTaskNotify(Uart_TaskHandle,OTA_STORAGE_ERROR_BIT,eSetBits);
}

static void W25Q_ReportCrcError(uint32_t session_id)// 上报整固件CRC32错误
{
	W25Q_Disable();
	if(W25Q_IsSessionCurrent(session_id) == 0U){
		return;
	}

	ota_storage_event_session_id = session_id;
	__DMB();
	xTaskNotify(Uart_TaskHandle, OTA_STORAGE_CRC_ERROR_BIT, eSetBits);
}

uint8_t W25Q_WaitReady(uint32_t TimeoutMs)
{
    TickType_t StartTick;
    TickType_t TimeOutTick;
    uint8_t BusyFlag = 1U;

    StartTick = xTaskGetTickCount();
    TimeOutTick = pdMS_TO_TICKS(TimeoutMs);

    while(1)
    {
        if(W25Q_EventBusy(&BusyFlag) == 0U)
        {
            return 0U;
        }

        if((BusyFlag & 0x01U) == 0U)
        {
            return 1U;
        }

        if((xTaskGetTickCount() - StartTick) >= TimeOutTick)
        {
            return 0U;
        }

        vTaskDelay(1U);
}
    }

void W25Q_WritrBINTask(void *arg)
{
	uint32_t SPI_NotifedValue;
	uint32_t len;
	uint32_t session_id;
	uint32_t active_session_id = 0xFFFFFFFFUL;
	uint32_t calculated_image_crc;
	BaseType_t NotifyResult;
	uint8_t temp_buffer[256];
	(void)arg;
	
	while(1)
	{
		SPI_NotifedValue = 0U;
		NotifyResult = xTaskNotifyWait(0U,
		                               SPI_DATA_BIT,
		                               &SPI_NotifedValue,
		                               portMAX_DELAY);
		if((NotifyResult != pdPASS) ||
		   ((SPI_NotifedValue & SPI_DATA_BIT) == 0U)){
			continue;
		}

		session_id = ota_storage_request_session_id;
		if(W25Q_IsSessionCurrent(session_id) == 0U){
			continue;
		}

		if(active_session_id != session_id){
			W25Q_ResetWriteAddress();
			active_session_id = session_id;
		}

		len = Get_Ring_Buffer_Lenth();
		if((SPI_NotifedValue & SPI_DATA_BIT) != 0U){
			while(len > 0){
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					break;
				}

				uint16_t page_remain = W25Q64_PageSize - (W25Q64Add % W25Q64_PageSize);
				uint16_t write_len = (len < page_remain) ? (uint16_t)len : page_remain;

				if(Ring_Buffer_Read(temp_buffer,write_len) == 0){
					W25Q_ReportstorageError(session_id);
					break;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					break;
				}
				if(W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0){
					 W25Q_ReportstorageError(session_id);
					break;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					break;
				}
				
				if(W25Q64Add % 0x1000 == 0){
					if(W25Q_EraseSector(W25Q64Add) != 1){
						W25Q_ReportstorageError(session_id);
						break;
					}
					if(W25Q_IsSessionCurrent(session_id) == 0U){
						W25Q_Disable();
						break;
					}
					if(W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0){
					 W25Q_ReportstorageError(session_id);
					break;
					}
					if(W25Q_IsSessionCurrent(session_id) == 0U){
						W25Q_Disable();
						break;
					}
				}
				if(W25Q_WritePage(W25Q64Add,temp_buffer,write_len) == 0){
					W25Q_ReportstorageError(session_id);
					break;
					
				}	
				if(W25Q_IsSessionCurrent(session_id) == 0U)
					{
						W25Q_Disable();
						break;
					}
					W25Q64Add += write_len;
					len -= write_len;
				}

			if(W25Q_IsSessionCurrent(session_id) == 0U){
				W25Q_Disable();
				continue;
			}
			if(len != 0U){
				continue;
			}

			if(W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0U){
				W25Q_ReportstorageError(session_id);
				continue;
			}
			if(W25Q_IsSessionCurrent(session_id) == 0U){
				W25Q_Disable();
				continue;
			}

			if(ota_last_packet != 0U){
				uint8_t cmd[4];

				if(W25Q64Add != (OTA_IMAGE_ADDRESS + packet.pkt_size)){
					W25Q_ReportstorageError(session_id);
					continue;
				}

				calculated_image_crc = 0U;
				if(W25Q_CalculateImageCrc32(OTA_IMAGE_ADDRESS,
												packet.pkt_size,
												&calculated_image_crc) == 0U){
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}
				if(calculated_image_crc != packet.image_crc32){
					W25Q_ReportCrcError(session_id);
					continue;
				}
				if(OTA_WriteStoredFirmwareVersion(packet.mcu_fw_ver) == 0U){
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					continue;
				}

				cmd[0] = W25Q64_WritePage;
				cmd[1] = 0x00U;
				cmd[2] = 0x00U;
				cmd[3] = 0x00U;

				if(W25Q_EraseSector(OTA_INFO_ADDRESS) == 0U){// 均为异常判断
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}
				if(W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0U){
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}
				if(W25Q_WriteEnable() == 0U){
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}

				W25Q_Enable();
				if(HAL_SPI_Transmit(&hspi1,cmd,4U,100U) != HAL_OK){
					W25Q_Disable();
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}
				if(HAL_SPI_Transmit(&hspi1,uart_data,sizeof(uart_data),100U) != HAL_OK){
					W25Q_Disable();
					W25Q_ReportstorageError(session_id);
					continue;
				}
				W25Q_Disable();
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					continue;
				}
				if(W25Q_WaitReady(OTA_STORAGE_TIMEOUT_MS) == 0U){
					W25Q_ReportstorageError(session_id);
					continue;
				}
				if(W25Q_IsSessionCurrent(session_id) == 0U){
					W25Q_Disable();
					continue;
				}
			}

			W25Q_ReportstorageDone(session_id);
		}
	}	
}

