#ifndef CAN_TASK_H
#define CAN_TASK_H

#include <stdint.h>

#include "FreeRTOS.h"
#include "stm32f4xx_hal_can.h"
#include "task.h"

#define CAN_RX_QUEUE_LENGTH 256U
#define CAN_TX_QUEUE_LENGTH 64U
//节点一
#define CAN_NODE1_HEARTBEAT_ID   0x210U
#define CAN_NODE1_DATA_ID        0x310U
//节点二
#define CAN_NODE2_HEARTBEAT_ID   0x220U
#define CAN_NODE2_DATA_ID        0x320U

#define CAN_NODE_OFFLINE_TIMEOUT_MS    3000
#define CAN_NODE_CHECK_PERIOD_MS       100
#define CAN_NODE_DATA_TIMEOUT_MS       500

/* Node 1 valid data range */
#define CAN_NODE1_TEMP_MIN_X100       2500
#define CAN_NODE1_TEMP_MAX_X100       3500
#define CAN_NODE1_HUMIDITY_MIN_X100   5000U
#define CAN_NODE1_HUMIDITY_MAX_X100   7000U
#define CAN_NODE1_VOLTAGE_MIN_MV      3250U
#define CAN_NODE1_VOLTAGE_MAX_MV      3350U

/* Node 2 valid data range */
#define CAN_NODE2_SPEED_MIN_RPM       1000U
#define CAN_NODE2_SPEED_MAX_RPM       2000U
#define CAN_NODE2_CURRENT_MIN_MA      1000U
#define CAN_NODE2_CURRENT_MAX_MA      1500U
#define CAN_NODE2_VIBRATION_MIN_MG    20U
#define CAN_NODE2_VIBRATION_MAX_MG    30U

typedef struct
{
    uint32_t ID;
    uint8_t IDE;
    uint8_t RTR;
    uint8_t DLC;
    uint8_t DATA[8];
} Can_Frame_t;

extern volatile uint32_t Can_Rx_Frame_Count;
extern volatile uint32_t Can_Rx_Processed_Count;
extern volatile uint32_t Can_Rx_Drop_Count;
extern volatile uint32_t Can_Rx_Fifo_Full_Count;
extern volatile uint32_t Can_Rx_Hardware_Overrun_Count;
extern volatile uint32_t Can_Error_Count;
extern volatile uint32_t Can_BusOff_Count;
extern volatile uint32_t Can_Tx_Submit_Count;
extern volatile uint32_t Can_Tx_Submit_Fail_Count;
extern volatile uint32_t Can_Tx_Queue_Drop_Count;
extern volatile uint32_t Can_Tx_Reject_Count;

extern volatile uint8_t Can_Node1_DataValid;
extern volatile uint8_t Can_Node2_DataValid;
extern volatile int16_t Can_Node1_Temperature;
extern volatile uint16_t Can_Node1_Humidity_x100;
extern volatile uint16_t Can_Node1_Voltage_mV;
extern volatile uint16_t Can_Node2_Speed_rpm;
extern volatile uint16_t Can_Node2_Current_mA;
extern volatile uint16_t Can_Node2_Vibration_mg;

uint8_t Can_Config(void);//配置硬件过滤器

BaseType_t Can_Task_Init(UBaseType_t RxPriority, UBaseType_t TxPriority);//初始化can

BaseType_t Can_SendFrame(const Can_Frame_t *Frame, TickType_t Timeout);//把发送数据添加到发送队列中

#endif
