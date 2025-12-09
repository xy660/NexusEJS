#ifdef ESP32_PLATFORM

#include <Arduino.h>
#include <VM.h>
#include <GC.h>
#include "ESP32Driver/Esp32Driver.h"


void ESP32_Platform_Init(){
    Serial.begin(9600);
    Serial.println("VM inited");

    

    // 实现ESP32 Arduino FreeRTOS平台的抽象层
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
        struct Param
        {
            void* prm;
            ThreadEntry entry;
            Param(void* p,ThreadEntry entry){
                this->prm = p;
                this->entry = entry;
            }
        };
        TaskHandle_t taskHandle = NULL;
        BaseType_t result = xTaskCreate(
            [](void* param){
                Param p = *(Param*)param;
                delete (Param*)param;
                p.entry(p.prm);
                vTaskDelete(NULL);
            }, // 任务函数
            "VMWorker",            // 任务名称
            8192,                  // 堆栈大小（根据需要调整）
            new Param(param,entry),                 // 参数
            tskIDLE_PRIORITY + 1,  // 优先级
            &taskHandle            // 任务句柄
        );
        if (result == pdPASS)
        {
            return (uint32_t)taskHandle;
        }
        return 0; // 创建失败
    };

    platform.CurrentThreadId = []() -> uint32_t
    {
        return (uint32_t)xTaskGetCurrentTaskHandle();
    };

    platform.ThreadYield = []()
    {
        taskYIELD();
    };

    platform.ThreadSleep = [](uint32_t sleep_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(sleep_ms));
    };

    platform.TickCount32 = []() -> uint32_t
    {
        return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
    };

    platform.MutexCreate = []() -> void *
    {
        //return NULL;
        SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
        return (void *)mutex;
    };

    platform.MutexLock = [](void *mutex)
    {
        //Serial.println("fake lock");
        xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
    };

    platform.MutexTryLock = [](void* mutex) -> bool 
    {
        SemaphoreHandle_t xMutex = (SemaphoreHandle_t)mutex;
        return (xSemaphoreTake(xMutex, 0) == pdTRUE);
    };

    platform.MutexUnlock = [](void *mutex)
    {
        //Serial.println("fake unlock");
        xSemaphoreGive((SemaphoreHandle_t)mutex);
    };

    platform.MutexDestroy = [](void *mutex)
    {
        //Serial.println("fake destroy");
        vSemaphoreDelete((SemaphoreHandle_t)mutex);
    };

    platform.MemoryFreePercent = []() -> float {
        return (float)esp_get_free_heap_size() / (float)heap_caps_get_total_size(MALLOC_CAP_8BIT);
    };
}

void ESP32_GpioClass_Init(VM* VMInstance){
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
        pinMode(pin,INPUT_PULLDOWN);
        return CreateBooleanVariable(digitalRead(pin));
    });
    
    analogReadResolution(ANALOG_READ_RESOLUTION); //默认10bit分辨率
    //返回归一化的模拟量读取
    gpioClass->implement.objectImpl[L"readAnalog"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin,INPUT_PULLDOWN);
        
        return CreateNumberVariable((double)digitalRead(pin) / (double)ANALOG_READ_RESOLUTION);
    });
    //返回原始数据的模拟量读取
    gpioClass->implement.objectImpl[L"readAnalogRaw"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin,INPUT_PULLDOWN);
        
        return CreateNumberVariable(digitalRead(pin));
    });
    
    VMObject* pwmChannelsArray = VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
    InitPWMChannels();
    //初始化通道对象数组后压入绑定到脚本的API数组
    pwmChannelsArray->implement.arrayImpl.reserve(MAX_PWM_CHANNELS);
    for(int i = 0;i < MAX_PWM_CHANNELS;i++){
        pwmChannelsArray->implement.arrayImpl.push_back(CreateReferenceVariable(&pwmChannelObjects[i]));
    }
    gpioClass->implement.objectImpl[L"pwmChannels"] = CreateReferenceVariable(pwmChannelsArray);



    std::wstring className = L"Gpio";
    auto ref = CreateReferenceVariable(gpioClass);
    VMInstance->storeGlobalSymbol(className,ref);
}

void ESP32_PlatformStdlibImpl_Init(VM* VMInstance){
    ESP32_GpioClass_Init(VMInstance);
#if ESP32_WIFI_API_ENABLED
    ESP32_WiFiPlatformApi_Init(VMInstance);
#endif
}   

void ESP32Driver_Init(VM* VMInstance){
    VMInstance->currentGC->GCDisabled = true; //暂时禁用GC避免创建对象中途发生GC导致垂悬指针
    ESP32_PlatformStdlibImpl_Init(VMInstance);
    VMInstance->currentGC->GCDisabled = false; 
}

#endif