#ifndef ONENET_H
#define ONENET_H

#include "FreeRTOS.h"
#include "cmsis_os.h"

#define ONENET_MQTT_PASSWORD       "YOUR_ONENET_TOKEN"
#define ONENET_MQTT_CLIENT_ID      "YOUR_DEVICE_NAME"
#define ONENET_MQTT_USERNAME       "YOUR_PRODUCT_ID"
#define ONENET_MQTT_HOST           "mqtts.heclouds.com"
#define ONENET_MQTT_PORT           1883U
#define ONENET_MQTT_TOPIC          "$sys/YOUR_PRODUCT_ID/YOUR_DEVICE_NAME/thing/property/post"

#define ONENET_TASK_STACK_DEPTH     256U
#define ONENET_TASK_PRIORITY        (osPriorityBelowNormal + 1U)
#define ONENET_RETRY_PERIOD_MS      5000U
#define ONENET_PUBLISH_PERIOD_MS    30000U
#define ONENET_CAN_CACHE_TIMEOUT_MS 3000U
#define ONENET_JSON_MAX_LENGTH      256U
#define ONENET_RESPONSE_MAX_LENGTH  192U

typedef enum
{
    ONENET_STATE_OFFLINE = 0U,
    ONENET_STATE_CONNECTING,
    ONENET_STATE_ONLINE,
    ONENET_STATE_ERROR
} OneNet_State_t;

extern volatile OneNet_State_t OneNet_State;
extern volatile uint32_t OneNet_Connect_Count;
extern volatile uint32_t OneNet_Publish_Count;
extern volatile uint32_t OneNet_Error_Count;
extern volatile uint32_t OneNet_NoCanData_Count;
extern volatile uint16_t OneNet_LastPayloadLength;
extern volatile uint16_t OneNet_LastCommandLength;
extern volatile uint16_t OneNet_LastResponseLength;
extern volatile uint8_t OneNet_LastPublishStage;
extern volatile uint8_t OneNet_CanCacheValidMask;
extern char OneNet_LastPayloadBuffer[ONENET_JSON_MAX_LENGTH];
extern char OneNet_LastResponseBuffer[ONENET_RESPONSE_MAX_LENGTH];

BaseType_t OneNet_Task_Init(UBaseType_t Priority);
void OneNet_Task(void *Argument);

#endif
