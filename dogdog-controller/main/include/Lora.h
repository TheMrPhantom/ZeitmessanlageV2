#ifndef __LORA_H
#define __LORA_H

#include "LoraNetwork.h"

void HandleReceivedPacket(DogDogPacket *packet);
void LoraSyncTask(void *pvParameters);
void confirm_station_alive(DogDogPacket *packet);
void LoraStartupTask(void *pvParameters);

#endif // __LORA_H