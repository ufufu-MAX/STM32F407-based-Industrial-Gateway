#include "main.h"   
#include "uart.h"   
#include "usart.h"  
#include "W25Q64.h"
#include "string.h"   
#include "FreeRTOS.h"
#include "task.h"

extern volatile uint8_t ota_state;

volatile uint32_t ota_session_id = 0U;// 当前OTA对话ID
volatile uint32_t ota_storage_request_session_id = 0U;// 本次要求W25Q写入的数据属于哪个会话
volatile uint32_t ota_storage_event_session_id = 0U;// W25发回的完成或错误属于哪个会话

TaskHandle_t Uart_TaskHandle = NULL;
TaskHandle_t SPI_TaskHandle = NULL;
extern SPI_HandleTypeDef hspi1;
extern CRC_HandleTypeDef hcrc;
s_pke packet;// 指令数据包

volatile uint8_t uart_flag = 0;// 指令接收标志
volatile uint8_t uart_error_flag = 0;// UART错误标志

uint8_t uart_data[Uart_FW_buffer];// 指令接收数组

uint8_t Rx_buffer[Uart_buffer];// 小包数据接收数组

s_Ringbuffer buffer;

uint32_t received_size = 0U;// 已经确认并交给W25Q写入的固件有效数据量
volatile uint16_t current_rx_len = 0U;// 当前DMA搬运大小
volatile uint16_t ota_payload_len = 0U;// 当前数据帧声明的payload长度
volatile uint8_t ota_rx_stage = OTA_RX_STAGE_HEADER;// 当前DMA接收阶段
volatile uint8_t ota_rx_header_error = 0U;// 数据帧头部长度字段错误
uint16_t ota_expected_sequence = 0U;// 下一包期望序号
uint16_t ota_current_sequence = 0U;// 当前正在处理的序号
volatile uint8_t ota_last_packet = 0U;

static uint16_t OTA_ReadLe16(const uint8_t *data)
{
	return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t OTA_ReadLe32(const uint8_t *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |((uint32_t)data[2] <<16) | ((uint32_t)data[3] << 24);
}

static uint32_t OTA_CalculateCrc32(const uint8_t *data, uint16_t length)
{
	uint16_t offset = 0U;
	uint32_t word;
	uint32_t crc = 0xFFFFFFFFU;
	uint8_t first_word = 1U;
	uint8_t byte_index;

	while(offset < length){
		word = 0U;

		for(byte_index = 0U;
		    (byte_index < 4U) && (offset < length);
		    byte_index++){
			word |= ((uint32_t)data[offset]) << (8U * byte_index);
			offset++;
		}

		if(first_word != 0U){
			crc = HAL_CRC_Calculate(&hcrc, &word, 1U);
			first_word = 0U;
		}else{
			crc = HAL_CRC_Accumulate(&hcrc, &word, 1U);
		}
	}

	return crc;
}

static uint8_t OTA_SendDataResponse(uint8_t status,uint16_t sequence,uint16_t timeout)
{
	uint8_t response[3];

	response[0] = status;
	response[1] = (uint8_t)(sequence & 0xFFU);
	response[2] = (uint8_t)((sequence >> 8) & 0xFFU);

	if(HAL_UART_Transmit(&huart1, response, sizeof(response), timeout) != HAL_OK){
		HAL_UART_Abort(&huart1);
		return 0U;
	}

	return 1U;
}

static uint8_t OTA_StartDataHeaderReceive(void)
{
	ota_rx_stage = OTA_RX_STAGE_HEADER;
	ota_rx_header_error = 0U;
	ota_payload_len = 0U;
	ota_current_sequence = ota_expected_sequence;
	current_rx_len = OTA_DATA_HEADER_SIZE;

	return (HAL_UART_Receive_DMA(&huart1,Rx_buffer,OTA_DATA_HEADER_SIZE) == HAL_OK) ? 1U : 0U;
}

static uint8_t OTA_RearmDataReceiveAndRespond(uint8_t status,uint16_t sequence)
{
	if(OTA_StartDataHeaderReceive() == 0U){
		return 0U;
	}

	return OTA_SendDataResponse(status, sequence, 10U);
}

void OTA_UART_RxCpltFromISR(BaseType_t *HigherPriorityTaskWoken)
{
	uint16_t payload_len;

	if(ota_rx_stage == OTA_RX_STAGE_HEADER){
		payload_len = OTA_ReadLe16(&Rx_buffer[4]);

		if((payload_len == 0U) ||(payload_len > OTA_DATA_PAYLOAD_SIZE))
			{ota_rx_header_error = 1U;
			xTaskNotifyFromISR(Uart_TaskHandle,UART_DATA_BIT,eSetBits,HigherPriorityTaskWoken);
			return;
		}

		ota_payload_len = payload_len;
		ota_rx_stage = OTA_RX_STAGE_BODY;
		current_rx_len = (uint16_t)(payload_len + OTA_DATA_TRAILER_SIZE);

		if(HAL_UART_Receive_DMA(&huart1,&Rx_buffer[OTA_DATA_HEADER_SIZE],current_rx_len) != HAL_OK){
			xTaskNotifyFromISR(Uart_TaskHandle,UART_ERROR_BIT,eSetBits,HigherPriorityTaskWoken);
		}
		return;
	}

	xTaskNotifyFromISR(Uart_TaskHandle,UART_DATA_BIT,eSetBits,HigherPriorityTaskWoken);
}

uint8_t OTA_StartHeaderReceive(void)
{
	uart_flag = 0U;
	uart_error_flag = 0U;
	return (HAL_UART_Receive_IT(&huart1, uart_data, Uart_FW_buffer) == HAL_OK) ? 1U : 0U;
}

uint8_t ESP8266_SendCommand(uint8_t Cmd,uint16_t Max_Time)// 发送命令
{
	if(HAL_UART_Transmit(&huart1,&Cmd,1,Max_Time) != HAL_OK){
		HAL_UART_Abort(&huart1);
		return 0;
	}
	return 1;
}

void OTA_NotifyStorageDone(void)
{
	uint8_t status = (ota_last_packet != 0U) ? FW_Update_ACK_END : FW_Update_ACK;
	uint16_t response_sequence = ota_current_sequence;

	if(ota_last_packet == 0U){
		ota_expected_sequence++;
		if(OTA_StartDataHeaderReceive() == 0U){
			xTaskNotify(Uart_TaskHandle, UART_ERROR_BIT, eSetBits);
			return;
		}
	}

	if(OTA_SendDataResponse(status, response_sequence, 10U) == 0U){
		xTaskNotify(Uart_TaskHandle,UART_ERROR_BIT,eSetBits);
		return;
	}
	
	if(ota_last_packet != 0U){
		__DSB();
		NVIC_SystemReset();
	}
}

void Ring_Buffer_Init()
{
	buffer.Read_Index = 0;
	buffer.Write_Index = 0;
}


uint16_t Get_Ring_Buffer_Lenth()// 获取未处理数据
{
	return ((RING_BUFFER_SIZE - buffer.Read_Index + buffer.Write_Index) % RING_BUFFER_SIZE);
}


uint16_t Get_Ring_Buffer_Remain()// 获取可写入大小
{
	return (RING_BUFFER_CAPACITY - Get_Ring_Buffer_Lenth());
}




uint8_t Ring_Buffer_Write(uint8_t *rx_data,uint16_t Length)// 写入缓冲区数据
{
	if(Length > Get_Ring_Buffer_Remain()){
		return 0;
	}
	if((buffer.Write_Index + Length) < RING_BUFFER_SIZE){
		memcpy(buffer.data +buffer.Write_Index,rx_data,Length);
		buffer.Write_Index += Length;
	}else{
		uint16_t First_Length = RING_BUFFER_SIZE - buffer.Write_Index;
		memcpy(buffer.data + buffer.Write_Index,rx_data,First_Length);
		memcpy(buffer.data,rx_data + First_Length,Length - First_Length);
		buffer.Write_Index = Length - First_Length;
	}
	return 1;
}

uint8_t Ring_Buffer_Read(uint8_t *read_data,uint16_t Length)// 读取缓冲区数据
{
	if((RING_BUFFER_CAPACITY - Get_Ring_Buffer_Remain()) < Length){
		return 0 ;
	}
	
	if(Length < (RING_BUFFER_SIZE - buffer.Read_Index)){
		memcpy(read_data,buffer.data + buffer.Read_Index,Length);
		buffer.Read_Index += Length;
	}else{
		uint16_t First_Read = RING_BUFFER_SIZE - buffer.Read_Index;
		memcpy(read_data,buffer.data + buffer.Read_Index,First_Read);
		memcpy(read_data + First_Read,buffer.data,Length - First_Read);
		buffer.Read_Index = Length - First_Read;
	}
	return 1;
}

static void OTA_ClearUartNotiftications()
{
	 ulTaskNotifyValueClear(NULL,OTA_UART_NOTIFY_MASK);
	
     xTaskNotifyStateClear(NULL);
}

static void OTA_RestSession(uint8_t status){
	uint8_t data_session = (ota_state != OTA_STATE_IDLE) ? 1U : 0U;
	uint16_t response_sequence = ota_current_sequence;

	(void)HAL_UART_AbortReceive(&huart1);
	ota_session_id++;// 先让当前W25Q任务所持有的对话编号失效
	__DMB();
	OTA_ClearUartNotiftications();
	ota_state = OTA_STATE_IDLE;
	received_size = 0U;
	current_rx_len = 0U;
	ota_payload_len = 0U;
	ota_rx_stage = OTA_RX_STAGE_HEADER;
	ota_rx_header_error = 0U;
	ota_expected_sequence = 0U;
	ota_current_sequence = 0U;
	ota_last_packet = 0U;
	
	memset(&packet,0,sizeof(packet));
	Ring_Buffer_Init();

	if(data_session != 0U){
		(void)OTA_SendDataResponse(status, response_sequence, 10U);
	}else{
		(void)ESP8266_SendCommand(status, 10U);
	}
	
	while(1){
		
		(void)HAL_UART_AbortReceive(&huart1);
		OTA_ClearUartNotiftications();
		if(OTA_StartHeaderReceive() != 0U){
		return;	
			}		
		vTaskDelay(pdMS_TO_TICKS(OTA_HEADER_RETRY_MS));// 每隔20ms重试
		}
	
}

uint8_t parse_serial_data()
{
	uint32_t stored_fw_version = 0xFFFFFFFFUL; uint8_t stored_version_written = 0U;

	if(uart_error_flag != 0U){
		uart_error_flag = 0U;
		OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
		return 0U;
	}

	if(uart_flag == 1){
		uart_flag = 0;
		uint8_t cmd =  FW_Update_ACK;
		memset(&packet,0,sizeof(packet));
		if(uart_data[0] == FW_Update_Head1 && uart_data[1] == FW_Update_Head2 && uart_data[16] == FW_Update_Tail1 && uart_data[17] == FW_Update_Tail2){
			
			packet.mcu_fw_ver = OTA_ReadLe32(&uart_data[2]);
			if(OTA_ReadStoredFirmwareVersion(&stored_fw_version,&stored_version_written) == 0U){
				OTA_RestSession(OTA_STATUS_FLASH_ERR);
				return 0U;
			}
			if((stored_version_written != 0U) && (packet.mcu_fw_ver <= stored_fw_version)){
				OTA_RestSession(OTA_STATUS_VERSION_ERR);
				return 0U;
			}
			
//			packet.mcu_fw_ver |= uart_data[2];
//			packet.mcu_fw_ver |= uart_data[3] << 8;
//			packet.mcu_fw_ver |= uart_data[4] << 16;
//			packet.mcu_fw_ver |= uart_data[5] << 24; // 版本号
			
			
			packet.pkt_size = OTA_ReadLe32(&uart_data[6]);
			
//			packet.pkt_size |= uart_data[6];
//			packet.pkt_size |= uart_data[7] << 8;
//			packet.pkt_size |= uart_data[8] << 16;
//			packet.pkt_size |= uart_data[9] << 24; // bin文件总大小
			
			if(packet.pkt_size == 0 || (packet.pkt_size > OTA_MAX_FIRMWARE_SIZE)){
				OTA_RestSession(OTA_STATUS_FW_SIZE_ERR);
				return 0;
			}
			
			packet.fram_size = OTA_ReadLe16(&uart_data[10]);
			
//			packet.fram_size |= uart_data[10];
//			packet.fram_size |= uart_data[11] << 8; // 帧大小
			
			
			packet.image_crc32 = OTA_ReadLe32(&uart_data[12]);// BIN文件CRC校验数据
			
			if(packet.fram_size != FRAME_SIZE){
				OTA_RestSession(OTA_STATUS_FRAME_SIZE_ERR);
				return 0;
			}
			
			packet.packet_cnt = (uint16_t)((packet.pkt_size + packet.fram_size - 1U) /packet.fram_size);// 几次可以接收完成
			
			packet.end_pkt_size = (uint16_t)(packet.pkt_size % packet.fram_size);// 最后一包大小
			if(packet.end_pkt_size == 0){
				packet.end_pkt_size = packet.fram_size;
			}
			ota_session_id++;
			__DMB();
			OTA_ClearUartNotiftications();
			ota_state = OTA_STATE_RECEIVING;

			Ring_Buffer_Init();
			received_size = 0U;
			ota_expected_sequence = 0U;
			ota_current_sequence = 0U;
			ota_payload_len = 0U;
			ota_rx_stage = OTA_RX_STAGE_HEADER;
			ota_rx_header_error = 0U;
			ota_last_packet = 0U;

			if(OTA_StartDataHeaderReceive() == 0U){
				HAL_UART_Abort(&huart1);
				OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
				return 0;
			}
			if(ESP8266_SendCommand(cmd,10) == 0){
				OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
				return 0;
			}
			return 1;
		}else{
			OTA_RestSession(OTA_STATUS_FRAME_HEAD_TAIL_ERR);			
			ota_state = 0;
			return 0;
		}		
	}
	return 0;
}

void OTA_Receive_Task(void *arg)
{
	(void)arg;
	uint32_t Notify_Value = 0;
	BaseType_t Notify_Result = 0;
	TickType_t WaitTicks;
	
	for(;;){
		
		while(!parse_serial_data()){
		vTaskDelay(1);
		}

		while((ota_state == OTA_STATE_RECEIVING) || (ota_state == OTA_STATE_FINALIZING)){	
		Notify_Value = 0;
		if(ota_state == OTA_STATE_FINALIZING)
		{
			WaitTicks = pdMS_TO_TICKS(OTA_FINALIZE_TIMEOUT_MS);
		}
		else
		{
			WaitTicks = pdMS_TO_TICKS(OTA_DATA_TIMEOUT_MS);
		}
		Notify_Result = xTaskNotifyWait(0,OTA_UART_NOTIFY_MASK,&Notify_Value, WaitTicks);
		
		if(Notify_Result != pdPASS){// 等待超时
			if(ota_state == OTA_STATE_FINALIZING)
		{
			OTA_RestSession(OTA_STATUS_FLASH_ERR);
		}else{
			OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
		}
			break;
		}
		
		if(((Notify_Value & OTA_STORAGE_CRC_ERROR_BIT) != 0U) &&
		   (ota_storage_event_session_id == ota_session_id))
		{
			OTA_RestSession(OTA_STATUS_DATA_CRC_ERR);
			break;
		}

		if(((Notify_Value & OTA_STORAGE_ERROR_BIT) != 0U) &&
		   (ota_storage_event_session_id == ota_session_id))
		{
			OTA_RestSession(OTA_STATUS_FLASH_ERR);
			break;
		}
		if((Notify_Value & UART_ERROR_BIT) != 0){// UART/DMA错误
			
			OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
			break;
		}
		
		if(((Notify_Value & OTA_STORAGE_DONE_BIT) != 0U) &&
		   (ota_storage_event_session_id == ota_session_id))
		{
			OTA_NotifyStorageDone();
			continue;
		}
				
		if((Notify_Value & UART_DATA_BIT) != 0U){
			uint16_t frame_sequence;
			uint16_t declared_length;
			uint16_t crc_offset;
			uint32_t received_crc;
			uint32_t calculated_crc;
			uint32_t expected_offset;
			uint16_t expected_payload_len;
			uint8_t duplicate_frame = 0U;

			if(ota_state != OTA_STATE_RECEIVING){
				continue;
			}

			if(ota_rx_header_error != 0U){
				OTA_RestSession(OTA_STATUS_DATA_FRAME_ERR);
				break;
			}

			if(ota_rx_stage != OTA_RX_STAGE_BODY){
				OTA_RestSession(OTA_STATUS_DATA_FRAME_ERR);
				break;
			}

			frame_sequence = OTA_ReadLe16(&Rx_buffer[2]);
			declared_length = OTA_ReadLe16(&Rx_buffer[4]);
			ota_current_sequence = frame_sequence;
			crc_offset = (uint16_t)(OTA_DATA_HEADER_SIZE + declared_length);

			if((Rx_buffer[0] != OTA_DATA_HEAD1) ||
			   (Rx_buffer[1] != OTA_DATA_HEAD2) ||
			   (declared_length != ota_payload_len) ||
			   (Rx_buffer[crc_offset + 4U] != OTA_DATA_TAIL1) ||
			   (Rx_buffer[crc_offset + 5U] != OTA_DATA_TAIL2)){
				if(OTA_RearmDataReceiveAndRespond(OTA_STATUS_DATA_FRAME_ERR,
													 frame_sequence) == 0U){
					OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
					break;
				}
				continue;
			}

			received_crc = OTA_ReadLe32(&Rx_buffer[crc_offset]);
			calculated_crc = OTA_CalculateCrc32(
										&Rx_buffer[2],
										(uint16_t)(4U + declared_length));

			if(received_crc != calculated_crc){
				if(OTA_RearmDataReceiveAndRespond(OTA_STATUS_DATA_FRAME_ERR,
													 frame_sequence) == 0U){
					OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
					break;
				}
				continue;
			}

			if(frame_sequence == ota_expected_sequence){
				duplicate_frame = 0U;
			}else if((ota_expected_sequence > 0U) &&
					 (frame_sequence == (uint16_t)(ota_expected_sequence - 1U))){
				duplicate_frame = 1U;
			}else{
				OTA_RestSession(OTA_STATUS_SEQUENCE_ERR);
				break;
			}

			expected_offset = (uint32_t)frame_sequence * OTA_DATA_PAYLOAD_SIZE;
			if(expected_offset >= packet.pkt_size){
				OTA_RestSession(OTA_STATUS_SEQUENCE_ERR);
				break;
			}

			if((packet.pkt_size - expected_offset) >= OTA_DATA_PAYLOAD_SIZE){
				expected_payload_len = OTA_DATA_PAYLOAD_SIZE;
			}else{
				expected_payload_len =
					(uint16_t)(packet.pkt_size - expected_offset);
			}

			if(declared_length != expected_payload_len){
				if(OTA_RearmDataReceiveAndRespond(OTA_STATUS_DATA_FRAME_ERR,
													 frame_sequence) == 0U){
					OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
					break;
				}
				continue;
			}

			if(duplicate_frame != 0U){
				if(OTA_RearmDataReceiveAndRespond(FW_Update_ACK,
													 frame_sequence) == 0U){
					OTA_RestSession(OTA_STATUS_UART_DMA_ERR);
					break;
				}
				continue;
			}

			if(expected_offset != received_size){
				OTA_RestSession(OTA_STATUS_SEQUENCE_ERR);
				break;
			}

			if(Ring_Buffer_Write(&Rx_buffer[OTA_DATA_HEADER_SIZE],
									 declared_length) == 0U){
				OTA_RestSession(OTA_STATUS_BUFFER_FULL);
				break;
			}

			received_size += declared_length;

			if(received_size == packet.pkt_size){
				ota_last_packet = 1U;
				ota_state = OTA_STATE_FINALIZING;
			}else{
				ota_last_packet = 0U;
			}

			ota_storage_request_session_id = ota_session_id;
			__DMB();
			xTaskNotify(SPI_TaskHandle, SPI_DATA_BIT, eSetBits);
		}else{
			continue;
		}
		
		}			
	}	
}


