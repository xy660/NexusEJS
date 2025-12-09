#ifdef ESP8266_PLATFORM

#include <Arduino.h>
#include <VM.h>
#include <GC.h>
#include "ESP8266Driver/ESP8266Driver.h"


void ESP8266_Platform_Init(){
    Serial.begin(9600);
    Serial.println("VM inited");

    

    // 实现ESP8266 Arduino FreeRTOS平台的抽象层
    platform.MemoryAlloc = [](size_t size)
    {
        void *memory = malloc(size);
        return memory;
    };

    platform.MemoryFree = [](void *ptr)
    {
        free(ptr);
    };

    platform.StartThread = [](ThreadEntry entry, void *param) -> uint32_t
    {
        
        return 0; // 创建失败
    };

    platform.CurrentThreadId = []() -> uint32_t
    {
        return 0;
    };

    platform.ThreadYield = []()
    {
        return;
    };

    platform.ThreadSleep = [](uint32_t sleep_ms)
    {
        delay(sleep_ms);
    };

    platform.TickCount32 = []() -> uint32_t
    {
        return millis();
    };

    platform.MutexCreate = []() -> void *
    {
        return (void*)123;
    };

    platform.MutexLock = [](void *mutex)
    {
        return;
    };

    platform.MutexTryLock = [](void* mutex) -> bool 
    {
        return true;
    };

    platform.MutexUnlock = [](void *mutex)
    {
        return;
    };

    platform.MutexDestroy = [](void *mutex)
    {
        return;
    };

    platform.MemoryFreePercent = []() -> float {
        return (float)0.1f;
    };
}

void ESP8266_GpioClass_Init(VM* VMInstance){
    VMObject* gpioClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
    gpioClass->implement.objectImpl[L"set"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM || args[1].getContentType() != ValueType::BOOL){
            currentWorker->ThrowError(L"Gpio.set: invalid arguments");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        uint8_t val = args[1].content.boolean ? HIGH : LOW;
        pinMode(pin,OUTPUT);
        digitalWrite(pin,val);
        return VariableValue();
    });
    gpioClass->implement.objectImpl[L"read"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin,INPUT);
        return CreateBooleanVariable(digitalRead(pin));
    });
    //analogReadResolution(ANALOG_READ_RESOLUTION); //默认10bit分辨率
    //返回归一化的模拟量读取
    gpioClass->implement.objectImpl[L"readAnalog"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin,INPUT);
        
        return CreateNumberVariable((double)digitalRead(pin) / (double)ANALOG_READ_RESOLUTION);
    });
    //返回原始数据的模拟量读取
    gpioClass->implement.objectImpl[L"readAnalogRaw"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin,INPUT);
        
        return CreateNumberVariable(digitalRead(pin));
    });
    


    std::wstring className = L"Gpio";
    auto ref = CreateReferenceVariable(gpioClass);
    VMInstance->storeGlobalSymbol(className,ref);
}

void ESP8266_PlatformStdlibImpl_Init(VM* VMInstance){
    ESP8266_GpioClass_Init(VMInstance);
#if ESP8266_WIFI_API_ENABLED
    ESP8266_WiFiPlatformApi_Init(VMInstance);
#endif
}   

void ESP8266Driver_Init(VM* VMInstance){
    VMInstance->currentGC->GCDisabled = true; //暂时禁用GC避免创建对象中途发生GC导致垂悬指针
    ESP8266_PlatformStdlibImpl_Init(VMInstance);
    VMInstance->currentGC->GCDisabled = false; 
}

#endif