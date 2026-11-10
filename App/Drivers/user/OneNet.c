#include "OneNet.h"
#include "usart.h"
#include "can_task.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define ONENET_LINE_END                 "\r\n"
#define ONENET_COMMAND_TIMEOUT_MS       3000U
#define ONENET_RAW_PROMPT_DELAY_MS     1000U
#define ONENET_RAW_RESULT_TIMEOUT_MS    5000U
#define ONENET_TX_TIMEOUT_MS            1000U
#define ONENET_RX_POLL_MS               20U
#define ONENET_NO_CAN_DATA_RETRY_MS      1000U
#define ONENET_PUBLISH_RESULT_ERROR      0U
#define ONENET_PUBLISH_RESULT_OK         1U
#define ONENET_PUBLISH_RESULT_NO_DATA    2U

volatile OneNet_State_t OneNet_State = ONENET_STATE_OFFLINE;
volatile uint32_t OneNet_Connect_Count = 0U;
volatile uint32_t OneNet_Publish_Count = 0U;
volatile uint32_t OneNet_Error_Count = 0U;
volatile uint32_t OneNet_NoCanData_Count = 0U;
volatile uint16_t OneNet_LastPayloadLength = 0U;
volatile uint16_t OneNet_LastCommandLength = 0U;
volatile uint16_t OneNet_LastResponseLength = 0U;
volatile uint8_t OneNet_LastPublishStage = 0U;
volatile uint8_t OneNet_CanCacheValidMask = 0U;

static TaskHandle_t OneNet_TaskHandle = NULL;
static char OneNet_CommandBuffer[384];
char OneNet_LastResponseBuffer[ONENET_RESPONSE_MAX_LENGTH];
char OneNet_LastPayloadBuffer[ONENET_JSON_MAX_LENGTH];

typedef struct
{
    int16_t Temperature_x100;
    uint16_t Humidity_x100;
    uint16_t Voltage_mV;
    uint16_t Speed_rpm;
    uint16_t Current_mA;
    uint16_t Vibration_mg;
} OneNet_CanData_t;

static OneNet_CanData_t OneNet_CanDataCache;
static TickType_t OneNet_Node1CacheTick = 0U;
static TickType_t OneNet_Node2CacheTick = 0U;

static uint8_t OneNet_SendBytes(const uint8_t *Data, uint16_t Length)
{
    if((Data == NULL) || (Length == 0U))
    {
        return 0U;
    }

    return (HAL_UART_Transmit(&huart2, (uint8_t *)Data, Length, ONENET_TX_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

static uint8_t OneNet_ReadResponse(char *Response, uint16_t ResponseSize, const char *Success, const char *Failure, uint32_t TimeoutMs)
{
    TickType_t StartTick = xTaskGetTickCount();
    TickType_t TimeoutTicks = pdMS_TO_TICKS(TimeoutMs);
    uint16_t Length = 0U;
    uint8_t Byte = 0U;

    if((Response == NULL) || (ResponseSize < 2U) || (Success == NULL))
    {
        return 0U;
    }

    Response[0] = '\0';
    while((TickType_t)(xTaskGetTickCount() - StartTick) < TimeoutTicks)
    {
        if(HAL_UART_Receive(&huart2, &Byte, 1U, ONENET_RX_POLL_MS) == HAL_OK)
        {
            if(Length < (uint16_t)(ResponseSize - 1U))
            {
                Response[Length++] = (char)Byte;
            }
            else
            {
                memmove(Response, &Response[1], (size_t)(ResponseSize - 2U));
                Response[ResponseSize - 2U] = (char)Byte;
                Length = (uint16_t)(ResponseSize - 1U);
            }
            Response[Length] = '\0';
            OneNet_LastResponseLength = Length;

            if(strstr(Response, Success) != NULL)
            {
                return 1U;
            }

            if((Failure != NULL) && (strstr(Response, Failure) != NULL))
            {
                return 0U;
            }
        }
    }

    return 0U;
}

static uint8_t OneNet_SendCommand(const char *Command, const char *Success, uint32_t TimeoutMs)
{
    uint16_t Length;

    if((Command == NULL) || (Success == NULL))
    {
        return 0U;
    }

    Length = (uint16_t)strlen(Command);
    if(Length == 0U)
    {
        return 0U;
    }

    if(OneNet_SendBytes((const uint8_t *)Command, Length) == 0U)
    {
        return 0U;
    }

    return OneNet_ReadResponse(OneNet_LastResponseBuffer, sizeof(OneNet_LastResponseBuffer), Success, "ERROR", TimeoutMs);
}

static uint8_t OneNet_Connect(void)
{
    int CommandLength;

    OneNet_State = ONENET_STATE_CONNECTING;

    if(OneNet_SendCommand("AT\r\n", "OK", ONENET_COMMAND_TIMEOUT_MS) == 0U)
    {
        return 0U;
    }

    CommandLength = snprintf(OneNet_CommandBuffer, sizeof(OneNet_CommandBuffer),
                             "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"\r\n",
                             ONENET_MQTT_CLIENT_ID, ONENET_MQTT_USERNAME, ONENET_MQTT_PASSWORD);
    if((CommandLength <= 0) || (CommandLength >= (int)sizeof(OneNet_CommandBuffer)))
    {
        return 0U;
    }
    if(OneNet_SendCommand(OneNet_CommandBuffer, "OK", ONENET_COMMAND_TIMEOUT_MS) == 0U)
    {
        return 0U;
    }

    CommandLength = snprintf(OneNet_CommandBuffer, sizeof(OneNet_CommandBuffer),
                             "AT+MQTTCONN=0,\"%s\",%u,1\r\n",
                             ONENET_MQTT_HOST, (unsigned int)ONENET_MQTT_PORT);
    if((CommandLength <= 0) || (CommandLength >= (int)sizeof(OneNet_CommandBuffer)))
    {
        return 0U;
    }
    if(OneNet_SendCommand(OneNet_CommandBuffer, "+MQTTCONNECTED", ONENET_COMMAND_TIMEOUT_MS) == 0U)
    {
        return 0U;
    }

    OneNet_State = ONENET_STATE_ONLINE;
    OneNet_Connect_Count++;
    return 1U;
}

static void OneNet_UpdateCanDataCache(void)
{
    TickType_t NowTick = xTaskGetTickCount();

    taskENTER_CRITICAL();
    if(Can_Node1_DataValid != 0U)
    {
        OneNet_CanDataCache.Temperature_x100 = Can_Node1_Temperature;
        OneNet_CanDataCache.Humidity_x100 = Can_Node1_Humidity_x100;
        OneNet_CanDataCache.Voltage_mV = Can_Node1_Voltage_mV;
        OneNet_Node1CacheTick = NowTick;
        OneNet_CanCacheValidMask |= 0x01U;
    }
    if(Can_Node2_DataValid != 0U)
    {
        OneNet_CanDataCache.Speed_rpm = Can_Node2_Speed_rpm;
        OneNet_CanDataCache.Current_mA = Can_Node2_Current_mA;
        OneNet_CanDataCache.Vibration_mg = Can_Node2_Vibration_mg;
        OneNet_Node2CacheTick = NowTick;
        OneNet_CanCacheValidMask |= 0x02U;
    }
    taskEXIT_CRITICAL();
}

static uint8_t OneNet_ReadCanData(OneNet_CanData_t *Data)
{
    TickType_t NowTick = xTaskGetTickCount();
    TickType_t CacheTimeoutTicks = pdMS_TO_TICKS(ONENET_CAN_CACHE_TIMEOUT_MS);

    if(Data == NULL)
    {
        return 0U;
    }

    taskENTER_CRITICAL();
    if((OneNet_CanCacheValidMask != 0x03U) ||
       ((TickType_t)(NowTick - OneNet_Node1CacheTick) >= CacheTimeoutTicks) ||
       ((TickType_t)(NowTick - OneNet_Node2CacheTick) >= CacheTimeoutTicks))
    {
        taskEXIT_CRITICAL();
        return 0U;
    }
    *Data = OneNet_CanDataCache;
    taskEXIT_CRITICAL();
    return 1U;
}

static uint8_t OneNet_PublishCanData(void)
{
    OneNet_CanData_t CanData;
    uint16_t PayloadLength;
    int32_t TemperatureAbs_x100;
    const char *TemperatureSign;
    int JsonLength;
    int CommandLength;

    if(OneNet_ReadCanData(&CanData) == 0U)
    {
        OneNet_LastPublishStage = 1U;
        return ONENET_PUBLISH_RESULT_NO_DATA;
    }

    TemperatureAbs_x100 = (int32_t)CanData.Temperature_x100;
    TemperatureSign = "";
    if(TemperatureAbs_x100 < 0)
    {
        TemperatureSign = "-";
        TemperatureAbs_x100 = -TemperatureAbs_x100;
    }

    JsonLength = snprintf(OneNet_LastPayloadBuffer, sizeof(OneNet_LastPayloadBuffer),
                          "{\"id\":\"123\",\"params\":{\"current\":{\"value\":%lu.%03lu},\"humidity\":{\"value\":%lu.%02lu},\"speed\":{\"value\":%lu},\"temp\":{\"value\":%s%ld.%02lu},\"vibration\":{\"value\":%lu},\"voltage\":{\"value\":%lu.%03lu}}}",
                          (unsigned long)(CanData.Current_mA / 1000U),
                          (unsigned long)(CanData.Current_mA % 1000U),
                          (unsigned long)(CanData.Humidity_x100 / 100U),
                          (unsigned long)(CanData.Humidity_x100 % 100U),
                          (unsigned long)CanData.Speed_rpm,
                          TemperatureSign,
                          (long)(TemperatureAbs_x100 / 100L),
                          (unsigned long)(TemperatureAbs_x100 % 100L),
                          (unsigned long)CanData.Vibration_mg,
                          (unsigned long)(CanData.Voltage_mV / 1000U),
                          (unsigned long)(CanData.Voltage_mV % 1000U));
    if((JsonLength <= 0) || (JsonLength >= (int)sizeof(OneNet_LastPayloadBuffer)))
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }

    PayloadLength = (uint16_t)strlen(OneNet_LastPayloadBuffer);
    if(PayloadLength != (uint16_t)JsonLength)
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }
    OneNet_LastPayloadLength = PayloadLength;
    OneNet_LastPublishStage = 2U;

    CommandLength = snprintf(OneNet_CommandBuffer, sizeof(OneNet_CommandBuffer),
                             "AT+MQTTPUBRAW=0,\"%s\",%u,0,0\r\n",
                             ONENET_MQTT_TOPIC, (unsigned int)PayloadLength);
    if((CommandLength <= 0) || (CommandLength >= (int)sizeof(OneNet_CommandBuffer)))
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }
    OneNet_LastCommandLength = (uint16_t)CommandLength;
    if(OneNet_SendBytes((const uint8_t *)OneNet_CommandBuffer, (uint16_t)CommandLength) == 0U)
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }
    OneNet_LastPublishStage = 3U;

    vTaskDelay(pdMS_TO_TICKS(ONENET_RAW_PROMPT_DELAY_MS));
    OneNet_LastPublishStage = 4U;

    if(OneNet_SendBytes((const uint8_t *)OneNet_LastPayloadBuffer, PayloadLength) == 0U)
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }
    OneNet_LastPublishStage = 5U;

    if(OneNet_ReadResponse(OneNet_LastResponseBuffer, sizeof(OneNet_LastResponseBuffer), "+MQTTPUB:OK", "ERROR", ONENET_RAW_RESULT_TIMEOUT_MS) == 0U)
    {
        return ONENET_PUBLISH_RESULT_ERROR;
    }

    OneNet_LastPublishStage = 6U;
    OneNet_Publish_Count++;
    return ONENET_PUBLISH_RESULT_OK;
}

BaseType_t OneNet_Task_Init(UBaseType_t Priority)
{
    if(OneNet_TaskHandle != NULL)
    {
        return pdFAIL;
    }

    return xTaskCreate(OneNet_Task,
                       "OneNet",
                       ONENET_TASK_STACK_DEPTH,
                       NULL,
                       Priority,
                       &OneNet_TaskHandle);
}

void OneNet_Task(void *Argument)
{
    TickType_t LastPublishStartTick = 0U;
    uint32_t PublishPeriodMs = ONENET_PUBLISH_PERIOD_MS;
    uint8_t PublishResult;

    (void)Argument;
    for(;;)
    {
        OneNet_UpdateCanDataCache();

        if(OneNet_State != ONENET_STATE_ONLINE)
        {
            if(OneNet_Connect() == 0U)
            {
                OneNet_State = ONENET_STATE_ERROR;
                OneNet_Error_Count++;
                vTaskDelay(pdMS_TO_TICKS(ONENET_RETRY_PERIOD_MS));
                continue;
            }
            LastPublishStartTick = xTaskGetTickCount();
            PublishPeriodMs = ONENET_PUBLISH_PERIOD_MS;
        }

        if((TickType_t)(xTaskGetTickCount() - LastPublishStartTick) >= pdMS_TO_TICKS(PublishPeriodMs))
        {
            LastPublishStartTick = xTaskGetTickCount();
            PublishResult = OneNet_PublishCanData();
            if(PublishResult == ONENET_PUBLISH_RESULT_ERROR)
            {
                OneNet_State = ONENET_STATE_ERROR;
                OneNet_Error_Count++;
            }
            else if(PublishResult == ONENET_PUBLISH_RESULT_NO_DATA)
            {
                OneNet_NoCanData_Count++;
                PublishPeriodMs = ONENET_NO_CAN_DATA_RETRY_MS;
            }
            else
            {
                PublishPeriodMs = ONENET_PUBLISH_PERIOD_MS;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100U));
    }
}
