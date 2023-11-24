/*
 * SensorController.c
 *
 *  Created on: Oct 24, 2022
 *      Author: kadh1
 */


#include <stdio.h>

#include "main.h"
#include "User/L2/Comm_Datalink.h"
#include "User/L3/AcousticSensor.h"
#include "User/L3/DepthSensor.h"
#include "User/L4/SensorPlatform.h"
#include "User/L4/SensorController.h"
#include "User/util.h"

#include "User/L1/USART_Driver.h"

//Required FreeRTOS header files
#include "FreeRTOS.h"
#include "Timers.h"
#include "semphr.h"

char printstring[50];

char* sensorNames[] = {"Acoustic", "Depth"};

SemaphoreHandle_t sensorMUTEX, pcMUTEX;

QueueHandle_t Queue_Sensor_Data;
QueueHandle_t Queue_HostPC_Data;


enum HostPCCommands MainPCCommand = PC_Command_NONE;




static void ResetMessageStruct(struct CommMessage* currentRxMessage){

	static const struct CommMessage EmptyMessage = {0};
	*currentRxMessage = EmptyMessage;
}


//helper functions to make the sensor controller task more readable
static void poll_queue(void * data, QueueHandle_t Queue, SemaphoreHandle_t semaphore){
	if(uxQueueMessagesWaiting(Queue)){//returns 0 if the queue is empty could be turned into a function
		xSemaphoreTake(semaphore, portMAX_DELAY);
		xQueueReceive(Queue, data, 0);
		xSemaphoreGive(pcMUTEX);
	}
}

static void print_sensor(struct CommMessage* currentRxMessage ){
	if(currentRxMessage->SensorID && currentRxMessage->SensorID- 2 < 3){//if the sensor id is 1 and to avoid out of bounds error for the sensorNames
		enum SensorId_t sensor = currentRxMessage->SensorID;
		uint16_t data = currentRxMessage->params;
		sprintf(printstring, "Got the data %30s %u \r\n", sensorNames[sensor - 2], data);
		print_str(printstring);
		ResetMessageStruct(currentRxMessage);
	}
}

/******************************************************************************
This task is created from the main.
******************************************************************************/
void SensorControllerTask(void *params)
{
	struct CommMessage currentRxMessage = {0};
	print_str("Controllertsk \r\n");

	do{
		enum HostPCCommands PCCommand = PC_Command_NONE;
		vTaskDelay(300 / portTICK_RATE_MS);

		while (PCCommand == PC_Command_NONE){
			poll_queue(&currentRxMessage, Queue_Sensor_Data, sensorMUTEX);//if the queue has acknowledge message receive them
			if(currentRxMessage.SensorID){//if a sensor id isn't 0
				print_sensor(&currentRxMessage);//print out the acknowledge messages
			}
			poll_queue(&PCCommand, Queue_HostPC_Data, pcMUTEX);//gets pc command from the pc
			vTaskDelay(300 / portTICK_RATE_MS);
		}

		switch (PCCommand){
		case PC_Command_START:
			send_sensorEnable_message(Acoustic, 5);
			send_sensorEnable_message(Depth, 2);
			vTaskDelay(300 / portTICK_RATE_MS);
			print_str("Enabled sensors \r\n");
			break;
		case PC_Command_RESET:
			send_sensorReset_message();
			vTaskDelay(300 / portTICK_RATE_MS);
			print_str("Disabled sensors \r\n");
			break;
		default:
			break;
		}

	}while(1);
}



/*
 * This task reads the queue of characters from the Sensor Platform when available
 * It then sends the processed data to the Sensor Controller Task
 */
void SensorPlatform_RX_Task(){
	struct CommMessage currentRxMessage = {0};

	sensorMUTEX = xSemaphoreCreateMutex();

	Queue_Sensor_Data = xQueueCreate(80, sizeof(struct CommMessage));

	request_sensor_read();  // requests a usart read (through the callback)

	while(1){
		parse_sensor_message(&currentRxMessage);

		if(currentRxMessage.IsMessageReady == true && currentRxMessage.IsCheckSumValid == true){
			//print_str("Got sensor data from buffer\r\n");
			xSemaphoreTake(sensorMUTEX, portMAX_DELAY);
			xQueueSendToBack(Queue_Sensor_Data, &currentRxMessage, 0);
			xSemaphoreGive(sensorMUTEX);

			ResetMessageStruct(&currentRxMessage);
		}
	}
}

/*
 * This task reads the queue of characters from the Host PC when available
 * It then sends the processed data to the Sensor Controller Task
 */
void HostPC_RX_Task(){

	enum HostPCCommands HostPCCommand = PC_Command_NONE;
	pcMUTEX = xSemaphoreCreateMutex();

	Queue_HostPC_Data = xQueueCreate(80, sizeof(enum HostPCCommands));

	request_hostPC_read();

	while(1){
		HostPCCommand = parse_hostPC_message();

		if (HostPCCommand != PC_Command_NONE){
			xSemaphoreTake(pcMUTEX, portMAX_DELAY);
			xQueueSendToBack(Queue_HostPC_Data, &HostPCCommand, 0);
			xSemaphoreGive(pcMUTEX);

		}

	}
}
