#ifdef ESP8266_PLATFORM

#pragma once

#include <Arduino.h>
#include "VM.h"


#define ESP8266_WIFI_API_ENABLED 0

#define ESP8266_WEBSERVER_ENABLED 0

#define MAX_PWM_CHANNELS 16
#define ANALOG_READ_RESOLUTION 1024


extern uint8_t pwmChannelObjectsBuffer[sizeof(VMObject) * MAX_PWM_CHANNELS];

extern VMObject* pwmChannelObjects;

#if ESP8266_WIFI_API_ENABLED
void ESP8266_WiFiPlatformApi_Init(VM* VMInstance);
#endif

void InitPWMChannels();
void ESP8266_Platform_Init();
void ESP8266Driver_Init(VM* VMInstance);

#endif