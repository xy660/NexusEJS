#ifdef ESP32_PLATFORM

#pragma once

#include <Arduino.h>
#include "VM.h"


#define ESP32_WIFI_API_ENABLED 1

#define ESP32_WEBSERVER_ENABLED 1

#define ESP32_HTTPCLIENT_ENABLED 0

#define ESP32_SOCKET_ENABLED 0

#define ESP32_I2C_ENABLED 1

#define ESP32_SPI_ENABLED 0 //待开发

#define MAX_PWM_CHANNELS 16
#define ANALOG_READ_RESOLUTION 1024


extern uint8_t pwmChannelObjectsBuffer[sizeof(VMObject) * MAX_PWM_CHANNELS];

extern VMObject* pwmChannelObjects;

#if ESP32_WIFI_API_ENABLED
void ESP32_WiFiPlatformApi_Init(VM* VMInstance);
#endif

void ESP32_ExtensionIO_Init(VM* VMInstance);
void InitPWMChannels();
void ESP32_Platform_Init();
void ESP32Driver_Init(VM* VMInstance);

#endif