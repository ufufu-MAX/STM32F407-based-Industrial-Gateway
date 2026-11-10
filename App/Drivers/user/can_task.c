#include "FreeRTOS.h"
#include "main.h"
#include "queue.h"
#include "can_task.h"
#include "task.h"

#define CAN_TASK_STACK_DEPTH       256U
#define CAN_TX_MAILBOX_WAIT_MS      10U

extern CAN_HandleTypeDef hcan1;

static QueueHandle_t Can_Rx_Queue;
static QueueHandle_t Can_Tx_Queue;
static TaskHandle_t Can_Tx_TaskHandle;

volatile uint32_t Can_Rx_Frame_Count = 0U;
volatile uint32_t Can_Rx_Processed_Count = 0U;
volatile uint32_t Can_Rx_Drop_Count = 0U;
volatile uint32_t Can_Rx_Fifo_Full_Count = 0U;
volatile uint32_t Can_Rx_Hardware_Overrun_Count = 0U;
volatile uint32_t Can_Error_Count = 0U;
volatile uint32_t Can_BusOff_Count = 0U;
volatile uint8_t Can_BusOff_Active = 0U;
volatile uint32_t Can_BusOff_Recover_Count = 0U;
volatile uint32_t Can_Tx_Submit_Count = 0U;
volatile uint32_t Can_Tx_Submit_Fail_Count = 0U;
volatile uint32_t Can_Tx_Queue_Drop_Count = 0U;
volatile uint32_t Can_Tx_Reject_Count = 0U;

volatile uint32_t Can_Node1_Heartbeat_Count = 0;//节点一心跳
volatile uint32_t Can_Node1_Data_Count = 0;//节点一数据
volatile uint32_t Can_Node2_Heartbeat_Count = 0;//节点二心跳
volatile uint32_t Can_Node2_Data_Count = 0;//节点二数据
volatile uint32_t Can_Unknown_ID_Count = 0;//未识别ID次数
volatile uint32_t Can_Invalid_DLC_Count = 0;//数据有效字节非8字节
volatile uint32_t Can_Node1_Range_Error_Count = 0U;//节点1数据范围错误计数
volatile uint32_t Can_Node2_Range_Error_Count = 0U;//节点2数据范围错误计数
volatile uint8_t Can_Node1_Range_Alarm = 0U;//节点1数据范围报警
volatile uint8_t Can_Node2_Range_Alarm = 0U;//节点2数据范围报警

volatile uint8_t Can_Node1_DataValid = 0;//数据是否有效
volatile uint8_t Can_Node2_DataValid = 0;

static TickType_t Can_Node1_LastDataTick = 0U;//最近一次收到数据的时间
static TickType_t Can_Node2_LastDataTick = 0U;

//节点一传感器数据 
volatile uint16_t Can_Node1_Sequence = 0U;//序号
volatile int16_t  Can_Node1_Temperature = 0;//原始值 = 温度x100
volatile uint16_t Can_Node1_Humidity_x100 = 0U;//湿度
volatile uint16_t Can_Node1_Voltage_mV = 0U;//电压

//节点2解析后的设备数据
volatile uint16_t Can_Node2_Sequence = 0U;//序号
volatile uint16_t Can_Node2_Speed_rpm = 0U;//转速
volatile uint16_t Can_Node2_Current_mA = 0U;//电流
volatile uint16_t Can_Node2_Vibration_mg = 0U;//震动

//节点1心跳数据
volatile uint32_t Can_Node1_Runtime_s = 0U;//运行时间
volatile uint16_t Can_Node1_Status = 0U;//节点状态
volatile uint16_t Can_Node1_Tx_Failure_Count = 0U;//发送失败累计次数

// 节点2心跳数据
volatile uint32_t Can_Node2_Runtime_s = 0U;//运行时间
volatile uint16_t Can_Node2_Status = 0U;//节点状态
volatile uint16_t Can_Node2_Tx_Failure_Count = 0U;//发送失败累计次数

volatile uint8_t Can_Node1_Online= 0;//1表示在线；0表示离线
volatile uint8_t Can_Node2_Online= 0;//1表示在线；0表示离线

static TickType_t Can_Node1_LastHeartbeatTick = 0U;//最近一次收到freertos节拍
static TickType_t Can_Node2_LastHeartbeatTick = 0U;//最近一次收到freertos节拍

//序号连续性统计
volatile uint32_t Can_Node1_Sequence_Error_Count = 0U;//序号错误计数
volatile uint32_t Can_Node2_Sequence_Error_Count = 0U;
volatile uint32_t Can_Node1_Restart_Count = 0U;//序号复位计数
volatile uint32_t Can_Node2_Restart_Count = 0U;

//只由CAN接收任务使用
static uint16_t Can_Node1_LastSequence = 0U;//序号计数
static uint16_t Can_Node2_LastSequence = 0U;
static uint8_t Can_Node1_SequenceValid = 0U;//序号有效计数
static uint8_t Can_Node2_SequenceValid = 0U;

static void Can_Rx_Task(void *arg);
static void Can_Tx_Task(void *arg);
static void Can_ProcessFrame(const Can_Frame_t *Frame);
static BaseType_t Can_IsFrameValid(const Can_Frame_t *Frame);
static HAL_StatusTypeDef Can_SubmitFrame(const Can_Frame_t *Frame);
static void Can_IncrementTaskCounter(volatile uint32_t *Counter);

uint8_t Can_Config(void)
{
    CAN_FilterTypeDef Filter = {0};

    Filter.FilterBank = 0;
    Filter.FilterMode = CAN_FILTERMODE_IDLIST;
    Filter.FilterScale = CAN_FILTERSCALE_16BIT;
    Filter.FilterIdLow = (uint16_t)(CAN_NODE1_HEARTBEAT_ID << 5U);
    Filter.FilterMaskIdLow = (uint16_t)(CAN_NODE1_DATA_ID << 5U);
    Filter.FilterIdHigh = (uint16_t)(CAN_NODE2_HEARTBEAT_ID << 5U);
    Filter.FilterMaskIdHigh = (uint16_t)(CAN_NODE2_DATA_ID << 5U);
    Filter.FilterFIFOAssignment = CAN_FILTER_FIFO0;
    Filter.FilterActivation = ENABLE;
    Filter.SlaveStartFilterBank = 14;

    return (HAL_CAN_ConfigFilter(&hcan1, &Filter) == HAL_OK) ? 1U : 0U;
}

BaseType_t Can_Task_Init(UBaseType_t RxPriority, UBaseType_t TxPriority)
{
    if((Can_Rx_Queue != NULL) || (Can_Tx_Queue != NULL)){
        return pdFAIL;
    }

    Can_Rx_Queue = xQueueCreate(CAN_RX_QUEUE_LENGTH, sizeof(Can_Frame_t));
    Can_Tx_Queue = xQueueCreate(CAN_TX_QUEUE_LENGTH, sizeof(Can_Frame_t));

    if((Can_Rx_Queue == NULL) || (Can_Tx_Queue == NULL)){
        return pdFAIL;
    }

    if(xTaskCreate(Can_Tx_Task,
                   "CanTx",
                   CAN_TASK_STACK_DEPTH,
                   NULL,
                   TxPriority,
                   &Can_Tx_TaskHandle) != pdPASS){
        return pdFAIL;
    }

    if(xTaskCreate(Can_Rx_Task,
                   "CanRx",
                   CAN_TASK_STACK_DEPTH,
                   NULL,
                   RxPriority,
                   NULL) != pdPASS){
        return pdFAIL;
    }

    return pdPASS;
}

BaseType_t Can_SendFrame(const Can_Frame_t *Frame, TickType_t Timeout)
{
    if((Can_Tx_Queue == NULL) || (Can_IsFrameValid(Frame) != pdTRUE)){
        Can_IncrementTaskCounter(&Can_Tx_Reject_Count);
        return pdFAIL;
    }

    if(xQueueSend(Can_Tx_Queue, Frame, Timeout) != pdPASS){
        Can_IncrementTaskCounter(&Can_Tx_Queue_Drop_Count);
        return pdFAIL;
    }

    return pdPASS;
}


static void Can_CheckSequence(uint16_t Sequence,uint16_t *LastSequence,uint8_t *SequenceValid,volatile uint32_t *ErrorCount,volatile uint32_t *RestartCount)
{
	uint16_t ExpectdeSequence = 0;
	
	if((*SequenceValid) == 0){
		*LastSequence = Sequence;
		*SequenceValid = 1;
		return;
	}
	
	ExpectdeSequence = (uint16_t)(*LastSequence + 1);

	if(Sequence != ExpectdeSequence){
		if((Sequence == 0U) && (ExpectdeSequence != 0U)){
            (*RestartCount)++;
        }else{
            (*ErrorCount)++;
        }
	}
	
	*LastSequence = Sequence;
	
}

static void Can_CheckNodeTimout()
{
	TickType_t DataTimeoutTick = pdMS_TO_TICKS(CAN_NODE_DATA_TIMEOUT_MS);
	TickType_t NowTick = xTaskGetTickCount();
	TickType_t TimouTick = pdMS_TO_TICKS(CAN_NODE_OFFLINE_TIMEOUT_MS);
	
	if((Can_Node1_Online != 0) && (TickType_t)(NowTick - Can_Node1_LastHeartbeatTick) >= TimouTick){
		Can_Node1_Online = 0U;
	}
	
	if((Can_Node2_Online != 0) && (TickType_t)(NowTick - Can_Node2_LastHeartbeatTick) >= TimouTick){
		Can_Node2_Online = 0U;
	}
	
	if((Can_Node1_DataValid != 0U) &&
   ((TickType_t)(NowTick - Can_Node1_LastDataTick) >= DataTimeoutTick)){
    Can_Node1_DataValid = 0U;
   }

	if((Can_Node2_DataValid != 0U) &&
	   ((TickType_t)(NowTick - Can_Node2_LastDataTick) >= DataTimeoutTick)){
		Can_Node2_DataValid = 0U;
	}
	
}

static void Can_CheckBusOffRecovery(void)
{
    if((Can_BusOff_Active != 0U) && (__HAL_CAN_GET_FLAG(&hcan1, CAN_FLAG_BOF) == RESET)){
        Can_BusOff_Active = 0U;
        Can_BusOff_Recover_Count++;
    }
}

static void Can_ProcessFrame(const Can_Frame_t *Frame)// 处理can数据
{
    if(Frame == NULL){
		return;
	}
	
	if((Frame->IDE != CAN_ID_STD) || (Frame->RTR != CAN_RTR_DATA)){
		Can_Unknown_ID_Count++;
		Can_Rx_Processed_Count++;
		return;
	}
	
	//DLC有效载荷判断
	if((Frame->DLC != 8U) &&((Frame->ID == CAN_NODE1_HEARTBEAT_ID) || (Frame->ID == CAN_NODE1_DATA_ID) || (Frame->ID == CAN_NODE2_HEARTBEAT_ID) || (Frame->ID == CAN_NODE2_DATA_ID))){
		Can_Invalid_DLC_Count++;
		Can_Rx_Processed_Count++;
		return;
	}
	
	switch(Frame->ID){
		 case CAN_NODE1_HEARTBEAT_ID:
			 if(Frame->DLC == 8){
				Can_Node1_Runtime_s =(uint32_t)Frame->DATA[0] |((uint32_t)Frame->DATA[1] << 8U) |((uint32_t)Frame->DATA[2] << 16U) |((uint32_t)Frame->DATA[3] << 24U);
				
				Can_Node1_Status =(uint16_t)Frame->DATA[4] |((uint16_t)Frame->DATA[5] << 8U);

				Can_Node1_Tx_Failure_Count =(uint16_t)Frame->DATA[6] |((uint16_t)Frame->DATA[7] << 8U);
				 
				Can_Node1_LastHeartbeatTick = xTaskGetTickCount();
				Can_Node1_Online = 1U;
			 }
             Can_Node1_Heartbeat_Count++;
            break;

        case CAN_NODE1_DATA_ID:
        {
            uint16_t Sequence; int16_t Temperature; uint16_t Humidity; uint16_t Voltage;

            Sequence = (uint16_t)Frame->DATA[0] | ((uint16_t)Frame->DATA[1] << 8U);
            Temperature = (int16_t)((uint16_t)Frame->DATA[2] | ((uint16_t)Frame->DATA[3] << 8U));
            Humidity = (uint16_t)Frame->DATA[4] | ((uint16_t)Frame->DATA[5] << 8U);
            Voltage = (uint16_t)Frame->DATA[6] | ((uint16_t)Frame->DATA[7] << 8U);

            Can_CheckSequence(Sequence,&Can_Node1_LastSequence,&Can_Node1_SequenceValid,&Can_Node1_Sequence_Error_Count,&Can_Node1_Restart_Count);

            if((Temperature >= CAN_NODE1_TEMP_MIN_X100) && (Temperature <= CAN_NODE1_TEMP_MAX_X100) && (Humidity >= CAN_NODE1_HUMIDITY_MIN_X100) && (Humidity <= CAN_NODE1_HUMIDITY_MAX_X100) && (Voltage >= CAN_NODE1_VOLTAGE_MIN_MV) && (Voltage <= CAN_NODE1_VOLTAGE_MAX_MV)){
                Can_Node1_Sequence = Sequence;
                Can_Node1_Temperature = Temperature;
                Can_Node1_Humidity_x100 = Humidity;
                Can_Node1_Voltage_mV = Voltage;
                Can_Node1_LastDataTick = xTaskGetTickCount();
                Can_Node1_DataValid = 1U;
                Can_Node1_Range_Alarm = 0U;
            }else{
                Can_Node1_Range_Error_Count++;
                Can_Node1_DataValid = 0U;
                Can_Node1_Range_Alarm = 1U;
            }

            Can_Node1_Data_Count++;
            break;
        }

        case CAN_NODE2_HEARTBEAT_ID:
			if(Frame->DLC == 8){
				Can_Node2_Runtime_s = (uint32_t)Frame->DATA[0] | ((uint32_t)Frame->DATA[1] << 8) | ((uint32_t)Frame->DATA[2] << 16) | ((uint32_t)Frame->DATA[3] << 24);
				
				Can_Node2_Status = (uint16_t)Frame->DATA[4] | ((uint16_t)Frame->DATA[5] << 8);
				
				Can_Node2_Tx_Failure_Count = (uint16_t)Frame->DATA[6] | ((uint16_t)Frame->DATA[7] << 8);
				
				Can_Node2_LastHeartbeatTick = xTaskGetTickCount();
				Can_Node2_Online = 1U;
			}
            Can_Node2_Heartbeat_Count++;
            break;

        case CAN_NODE2_DATA_ID:
        {
            uint16_t Sequence; uint16_t Speed; uint16_t Current; uint16_t Vibration;

            Sequence = (uint16_t)Frame->DATA[0] | ((uint16_t)Frame->DATA[1] << 8U);
            Speed = (uint16_t)Frame->DATA[2] | ((uint16_t)Frame->DATA[3] << 8U);
            Current = (uint16_t)Frame->DATA[4] | ((uint16_t)Frame->DATA[5] << 8U);
            Vibration = (uint16_t)Frame->DATA[6] | ((uint16_t)Frame->DATA[7] << 8U);

            Can_CheckSequence(Sequence,&Can_Node2_LastSequence,&Can_Node2_SequenceValid,&Can_Node2_Sequence_Error_Count,&Can_Node2_Restart_Count);

            if((Speed >= CAN_NODE2_SPEED_MIN_RPM) && (Speed <= CAN_NODE2_SPEED_MAX_RPM) && (Current >= CAN_NODE2_CURRENT_MIN_MA) && (Current <= CAN_NODE2_CURRENT_MAX_MA) && (Vibration >= CAN_NODE2_VIBRATION_MIN_MG) && (Vibration <= CAN_NODE2_VIBRATION_MAX_MG)){
                Can_Node2_Sequence = Sequence;
                Can_Node2_Speed_rpm = Speed;
                Can_Node2_Current_mA = Current;
                Can_Node2_Vibration_mg = Vibration;
                Can_Node2_LastDataTick = xTaskGetTickCount();
                Can_Node2_DataValid = 1U;
                Can_Node2_Range_Alarm = 0U;
            }else{
                Can_Node2_Range_Error_Count++;
                Can_Node2_DataValid = 0U;
                Can_Node2_Range_Alarm = 1U;
            }

            Can_Node2_Data_Count++;
            break;
        }

        default:
            Can_Unknown_ID_Count++;
            break;
	}
	
    Can_Rx_Processed_Count++;
}

static BaseType_t Can_IsFrameValid(const Can_Frame_t *Frame)// 检查数据是否合法
{
    if((Frame == NULL) || (Frame->DLC > 8U)){
        return pdFALSE;
    }

    if((Frame->RTR != CAN_RTR_DATA) && (Frame->RTR != CAN_RTR_REMOTE)){
        return pdFALSE;
    }

    if(Frame->IDE == CAN_ID_STD){
        return (Frame->ID <= 0x7FFU) ? pdTRUE : pdFALSE;
    }

    if(Frame->IDE == CAN_ID_EXT){
        return (Frame->ID <= 0x1FFFFFFFU) ? pdTRUE : pdFALSE;
    }

    return pdFALSE;
}

static HAL_StatusTypeDef Can_SubmitFrame(const Can_Frame_t *Frame)// CAN发送数据函数
{
    CAN_TxHeaderTypeDef Header = {0};
    uint32_t Mailbox = 0U;
    TickType_t StartTick;
    const TickType_t WaitTicks = pdMS_TO_TICKS(CAN_TX_MAILBOX_WAIT_MS);

    if(Can_IsFrameValid(Frame) != pdTRUE){
        return HAL_ERROR;
    }

    Header.IDE = Frame->IDE;
    Header.RTR = Frame->RTR;
    Header.DLC = Frame->DLC;
    Header.TransmitGlobalTime = DISABLE;

    if(Frame->IDE == CAN_ID_STD){
        Header.StdId = Frame->ID;
    }else{
        Header.ExtId = Frame->ID;
    }

    StartTick = xTaskGetTickCount();
    while(HAL_CAN_GetTxMailboxesFreeLevel(&hcan1) == 0U){
        if((xTaskGetTickCount() - StartTick) >= WaitTicks){
            return HAL_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }

    return HAL_CAN_AddTxMessage(&hcan1, &Header, Frame->DATA, &Mailbox);
}

static void Can_IncrementTaskCounter(volatile uint32_t *Counter)// 错误计数
{
    taskENTER_CRITICAL();
    (*Counter)++;
    taskEXIT_CRITICAL();
}

void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *hcan)// CAN回调函数
{
    BaseType_t TaskWoken = pdFALSE;

    if((hcan->Instance != CAN1) || (Can_Rx_Queue == NULL)){
        return;
    }

    while(HAL_CAN_GetRxFifoFillLevel(hcan, CAN_RX_FIFO0) > 0U){
        CAN_RxHeaderTypeDef Header = {0};
        Can_Frame_t Frame = {0};

        if(HAL_CAN_GetRxMessage(hcan,CAN_RX_FIFO0,&Header,Frame.DATA) != HAL_OK){
            break;
        }

        Frame.ID = (Header.IDE == CAN_ID_STD) ? Header.StdId : Header.ExtId;
        Frame.IDE = (uint8_t)Header.IDE;
        Frame.RTR = (uint8_t)Header.RTR;
        Frame.DLC = (uint8_t)Header.DLC;

        if(xQueueSendFromISR(Can_Rx_Queue, &Frame, &TaskWoken) == pdPASS){
            Can_Rx_Frame_Count++;
        }else{
            Can_Rx_Drop_Count++;
        }
    }
    portYIELD_FROM_ISR(TaskWoken);
}

void HAL_CAN_RxFifo0FullCallback(CAN_HandleTypeDef *hcan)//FIFO满中断
{
    if(hcan->Instance == CAN1){
        Can_Rx_Fifo_Full_Count++;
    }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *hcan)//CAN错误回调函数
{
    uint32_t ErrorCode;

    if(hcan->Instance != CAN1){
        return;
    }

    ErrorCode = HAL_CAN_GetError(hcan);
    Can_Error_Count++;

    if((ErrorCode & HAL_CAN_ERROR_RX_FOV0) != 0U){
        Can_Rx_Hardware_Overrun_Count++;
    }

    if((ErrorCode & HAL_CAN_ERROR_BOF) != 0U){
        Can_BusOff_Count++;
        Can_BusOff_Active = 1U;
    }

    hcan->ErrorCode = HAL_CAN_ERROR_NONE;
}

static void Can_Rx_Task(void *arg)
{
    Can_Frame_t Frame = {0};

    (void)arg;

    if(HAL_CAN_Start(&hcan1) != HAL_OK){
        Error_Handler();
    }

    if(HAL_CAN_ActivateNotification(&hcan1,CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_FULL | CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_ERROR | CAN_IT_BUSOFF) != HAL_OK){
        Error_Handler();
    }

    xTaskNotifyGive(Can_Tx_TaskHandle);

    for(;;){
		
        if(xQueueReceive(Can_Rx_Queue, &Frame, pdMS_TO_TICKS(CAN_NODE_CHECK_PERIOD_MS)) == pdPASS){
            Can_ProcessFrame(&Frame);
        }
		Can_CheckNodeTimout();
        Can_CheckBusOffRecovery();
    }
}

static void Can_Tx_Task(void *arg)// 发送任务
{
    Can_Frame_t Frame = {0};

    (void)arg;

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    for(;;){
        if(xQueueReceive(Can_Tx_Queue, &Frame, portMAX_DELAY) == pdPASS){
            if(Can_SubmitFrame(&Frame) == HAL_OK){
                Can_IncrementTaskCounter(&Can_Tx_Submit_Count);// 发送计数
				
            }else{
                Can_IncrementTaskCounter(&Can_Tx_Submit_Fail_Count);// 失败计数
            }
        }
    }
}
