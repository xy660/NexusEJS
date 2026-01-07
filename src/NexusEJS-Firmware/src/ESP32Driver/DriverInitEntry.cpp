#ifdef ESP32_PLATFORM

#include <Arduino.h>
#include <GC.h>
#include <SPIFFS.h>
#include <VM.h>
#include <driver/i2c.h>

#include "ESP32Driver/Esp32Driver.h"
#include "StringConverter.h"

bool _StartWith(const char* src, const char* header) {
  int srcLength = strlen(src);
  int headerLength = strlen(header);
  if (srcLength < headerLength) {
    return false;
  }
  for (int i = 0; i < headerLength; i++) {
    if (src[i] != header[i]) {
      return false;
    }
  }
  return true;
}

void ESP32_Platform_Init() {
  Serial.begin(DEFAULT_BAUDRATE);
  Serial.println("VM inited");

  // 实现ESP32 Arduino FreeRTOS平台的抽象层
  platform.MemoryAlloc = [](size_t size) {
    void* memory = malloc(size);
    return memory;
  };

  platform.MemoryFree = [](void* ptr) { free(ptr); };

  platform.StartThread = [](ThreadEntry entry, void* param) -> uint32_t {
    struct Param {
      void* prm;
      ThreadEntry entry;
      Param(void* p, ThreadEntry entry) {
        this->prm = p;
        this->entry = entry;
      }
    };
    TaskHandle_t taskHandle = NULL;
    BaseType_t result = xTaskCreate(
        [](void* param) {
          Param p = *(Param*)param;
          delete (Param*)param;
          p.entry(p.prm);
          vTaskDelete(NULL);
        },                        // 任务函数
        "VMWorker",               // 任务名称
        8192,                     // 堆栈大小（根据需要调整）
        new Param(param, entry),  // 参数
        tskIDLE_PRIORITY + 1,     // 优先级
        &taskHandle               // 任务句柄
    );
    if (result == pdPASS) {
      return (uint32_t)taskHandle;
    }
    return 0;  // 创建失败
  };

  platform.CurrentThreadId = []() -> uint32_t {
    return (uint32_t)xTaskGetCurrentTaskHandle();
  };

  platform.ThreadYield = []() { taskYIELD(); };

  platform.ThreadSleep = [](uint32_t sleep_ms) {
    vTaskDelay(pdMS_TO_TICKS(sleep_ms));
  };

  platform.TickCount32 = []() -> uint32_t {
    return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS;
  };

  platform.MutexCreate = []() -> void* {
    // return NULL;
    SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return (void*)mutex;
  };

  platform.MutexLock = [](void* mutex) {
    // Serial.println("fake lock");
    xSemaphoreTake((SemaphoreHandle_t)mutex, portMAX_DELAY);
  };

  platform.MutexTryLock = [](void* mutex) -> bool {
    SemaphoreHandle_t xMutex = (SemaphoreHandle_t)mutex;
    return (xSemaphoreTake(xMutex, 0) == pdTRUE);
  };

  platform.MutexUnlock = [](void* mutex) {
    // Serial.println("fake unlock");
    xSemaphoreGive((SemaphoreHandle_t)mutex);
  };

  platform.MutexDestroy = [](void* mutex) {
    // Serial.println("fake destroy");
    vSemaphoreDelete((SemaphoreHandle_t)mutex);
  };

  platform.MemoryFreePercent = []() -> float {
    float memoryFree = (float)esp_get_free_heap_size() /
           (float)heap_caps_get_total_size(MALLOC_CAP_8BIT);
    return memoryFree;
  };
  printf("FS.begin:%d\n", SPIFFS.begin(true));
  platform.FileExist = [](std::string& fileName) -> bool {
    return SPIFFS.exists(fileName.c_str());
  };
  platform.ReadFile = [](std::string& fileName,
                         uint32_t* fileSize) -> uint8_t* {
    auto file = SPIFFS.open(fileName.c_str());
    *fileSize = file.size();
    printf("FS.Read:%s size:%d\n", file.name(), file.size());
    char* buf = (char*)platform.MemoryAlloc(file.size());
    file.readBytes(buf, file.size());
    return (uint8_t*)buf;
  };

#if VM_DEBUGGER_ENABLED
  // 如果启用调试器就编译兼容层

  // static bool debuggerConnected = false;
  debuggerImpl.IsDebuggerConnected = []() -> bool { return true; };
  debuggerImpl.ReadFromDebugger = []() -> std::string {
    std::ostringstream stream;
    while (true) {
      if (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n') {
          if (_StartWith(stream.str().c_str(), "debugger:")) {
            return stream.str().substr(9);  // 删掉协议头
          } else {
            stream.str("");
          }
        } else {
          stream << c;
        }
      }
    }
  };
  debuggerImpl.SendToDebugger = [](const char* msg) -> void {
    static void* sendLock = NULL;
    if (!sendLock) {
      sendLock = platform.MutexCreate();
    }
    platform.MutexLock(sendLock);
    Serial.printf("debugger:%s\n", msg);
    platform.MutexUnlock(sendLock);
  };
  debuggerImpl.implemented = true;

#endif
}

void ESP32_GpioClass_Init(VM* VMInstance) {
  VMObject* gpioClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
  gpioClass->implement.objectImpl["set"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::BOOL) {
          currentWorker->ThrowError("Gpio.set: invalid arguments");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        uint8_t val = args[1].content.boolean ? HIGH : LOW;
        pinMode(pin, OUTPUT);
        digitalWrite(pin, val);
        return VariableValue();
      });
  gpioClass->implement.objectImpl["read"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin, INPUT_PULLDOWN);
        return CreateBooleanVariable(digitalRead(pin));
      });

  analogReadResolution(ANALOG_READ_RESOLUTION);  // 默认10bit分辨率
  // 返回归一化的模拟量读取
  gpioClass->implement.objectImpl["readAnalog"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin, INPUT_PULLDOWN);

        return CreateNumberVariable((double)digitalRead(pin) /
                                    (double)ANALOG_READ_RESOLUTION);
      });
  // 返回原始数据的模拟量读取
  gpioClass->implement.objectImpl["readAnalogRaw"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Gpio.read: pin must be number");
        }
        uint8_t pin = (uint8_t)args[0].content.number;
        pinMode(pin, INPUT_PULLDOWN);

        return CreateNumberVariable(digitalRead(pin));
      });

#if ESP32_PWM_ENALBED
  VMObject* pwmChannelsArray =
      VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
  InitPWMChannels();
  // 初始化通道对象数组后压入绑定到脚本的API数组
  pwmChannelsArray->implement.arrayImpl.reserve(MAX_PWM_CHANNELS);
  for (int i = 0; i < MAX_PWM_CHANNELS; i++) {
    pwmChannelsArray->implement.arrayImpl.push_back(
        CreateReferenceVariable(&pwmChannelObjects[i]));
  }
  gpioClass->implement.objectImpl["pwmChannels"] =
      CreateReferenceVariable(pwmChannelsArray);
#endif

  std::string className = "Gpio";
  auto ref = CreateReferenceVariable(gpioClass);
  VMInstance->storeGlobalSymbol(className, ref);
}

void ESP32_SystemApi_Init(VM* VMInstance) {
  VMObject* systemClass =
      VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);

  // 重启系统
  systemClass->implement.objectImpl["reboot"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        ESP.restart();
        return VariableValue();  // 不会执行到这里
      });

  // 重启系统（带延时）
  systemClass->implement.objectImpl["restart"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        ESP.restart();
        return VariableValue();
      });

  // 进入深度睡眠
  systemClass->implement.objectImpl["deepSleep"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "System.deepSleep: time must be number (microseconds)");
          return VariableValue();
        }

        uint64_t sleepTime = 0;
        if (args.size() > 0) {
          sleepTime = (uint64_t)args[0].content.number;
        }

        esp_deep_sleep(sleepTime);
        return VariableValue();
      });

  // 进入轻度睡眠
  systemClass->implement.objectImpl["lightSleep"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "System.lightSleep: time must be number (microseconds)");
          return VariableValue();
        }

        uint64_t sleepTime = 0;
        if (args.size() > 0) {
          sleepTime = (uint64_t)args[0].content.number;
        }

        esp_sleep_enable_timer_wakeup(sleepTime);
        esp_light_sleep_start();
        return VariableValue();
      });

  // 获取启动原因
  systemClass->implement.objectImpl["getBootReason"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        esp_reset_reason_t reason = esp_reset_reason();
        return CreateNumberVariable((double)reason);
      });

  // 获取唤醒原因
  systemClass->implement.objectImpl["getWakeupReason"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        return CreateNumberVariable((double)cause);
      });

  // 获取芯片信息
  systemClass->implement.objectImpl["getChipInfo"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        VMObject* infoObj = currentWorker->VMInstance->currentGC->GC_NewObject(
            ValueType::OBJECT);

        esp_chip_info_t chip_info;
        esp_chip_info(&chip_info);

        infoObj->implement.objectImpl["model"] =
            CreateNumberVariable((double)chip_info.model);
        infoObj->implement.objectImpl["cores"] =
            CreateNumberVariable((double)chip_info.cores);
        infoObj->implement.objectImpl["revision"] =
            CreateNumberVariable((double)chip_info.revision);

        uint32_t chipId = 0;
        for (int i = 0; i < 17; i = i + 8) {
          chipId |= ((ESP.getEfuseMac() >> (40 - i)) & 0xff) << i;
        }
        infoObj->implement.objectImpl["id"] =
            CreateNumberVariable((double)chipId);

        return CreateReferenceVariable(infoObj);
      });

  // 获取内存信息
  systemClass->implement.objectImpl["getMemoryInfo"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        VMObject* infoObj = currentWorker->VMInstance->currentGC->GC_NewObject(
            ValueType::OBJECT);

        infoObj->implement.objectImpl["heapSize"] =
            CreateNumberVariable((double)ESP.getHeapSize());
        infoObj->implement.objectImpl["freeHeap"] =
            CreateNumberVariable((double)ESP.getFreeHeap());
        infoObj->implement.objectImpl["minFreeHeap"] =
            CreateNumberVariable((double)ESP.getMinFreeHeap());
        infoObj->implement.objectImpl["maxAllocHeap"] =
            CreateNumberVariable((double)ESP.getMaxAllocHeap());

#ifdef ESP32
        infoObj->implement.objectImpl["psramSize"] =
            CreateNumberVariable((double)ESP.getPsramSize());
        infoObj->implement.objectImpl["freePsram"] =
            CreateNumberVariable((double)ESP.getFreePsram());
        infoObj->implement.objectImpl["minFreePsram"] =
            CreateNumberVariable((double)ESP.getMinFreePsram());
        infoObj->implement.objectImpl["maxAllocPsram"] =
            CreateNumberVariable((double)ESP.getMaxAllocPsram());
#endif

        return CreateReferenceVariable(infoObj);
      });

  // 获取启动时间（毫秒）
  systemClass->implement.objectImpl["getUptime"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        return CreateNumberVariable((double)millis());
      });

  // 获取启动时间（微秒）
  systemClass->implement.objectImpl["getUptimeMicros"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        return CreateNumberVariable((double)micros());
      });

  // 延迟函数（毫秒）
  systemClass->implement.objectImpl["delay"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "System.delay: time must be number (milliseconds)");
          return VariableValue();
        }

        

        if (args.size() > 0) {
          uint32_t delayTime = (uint32_t)args[0].content.number;
          currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Inc();
          vTaskDelay(delayTime / portTICK_PERIOD_MS);
          currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Dec();
        }

        

        return VariableValue();
      });

  // 延迟函数（微秒）
  systemClass->implement.objectImpl["delayMicroseconds"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "System.delayMicroseconds: time must be number (microseconds)");
          return VariableValue();
        }

        

        if (args.size() > 0) {
          uint32_t delayTime = (uint32_t)args[0].content.number;
          currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Inc();
          delayMicroseconds(delayTime);
          currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Dec();
        }

        

        return VariableValue();
      });

  // 启用/禁用看门狗
  systemClass->implement.objectImpl["watchdogEnable"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getContentType() != ValueType::BOOL) {
          currentWorker->ThrowError(
              "System.watchdogEnable: enable must be boolean");
          return VariableValue();
        }

        bool enable = true;
        if (args.size() > 0) {
          enable = args[0].content.boolean;
        }

        if (enable) {
          enableLoopWDT();
        } else {
          disableLoopWDT();
        }

        return CreateBooleanVariable(enable);
      });

  // 喂狗
  systemClass->implement.objectImpl["feedWatchdog"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        feedLoopWDT();
        return VariableValue();
      });

  systemClass->implement.objectImpl["gc"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        VariableValue ret;
        currentWorker->VMInstance->currentGC->GC_Collect();
        return VariableValue();
      });

  // 常量定义
  systemClass->implement.objectImpl["BOOT_REASON_UNKNOWN"] =
      CreateNumberVariable((double)ESP_RST_UNKNOWN);
  systemClass->implement.objectImpl["BOOT_REASON_POWERON"] =
      CreateNumberVariable((double)ESP_RST_POWERON);
  systemClass->implement.objectImpl["BOOT_REASON_RESET"] =
      CreateNumberVariable((double)ESP_RST_SW);
  systemClass->implement.objectImpl["BOOT_REASON_EXCEPTION"] =
      CreateNumberVariable((double)ESP_RST_PANIC);
  systemClass->implement.objectImpl["BOOT_REASON_WDT"] =
      CreateNumberVariable((double)ESP_RST_INT_WDT);
  systemClass->implement.objectImpl["BOOT_REASON_DEEPSLEEP"] =
      CreateNumberVariable((double)ESP_RST_DEEPSLEEP);
  systemClass->implement.objectImpl["BOOT_REASON_BROWNOUT"] =
      CreateNumberVariable((double)ESP_RST_BROWNOUT);
  systemClass->implement.objectImpl["BOOT_REASON_SDIO"] =
      CreateNumberVariable((double)ESP_RST_SDIO);

  systemClass->implement.objectImpl["WAKEUP_REASON_UNDEFINED"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_UNDEFINED);
  systemClass->implement.objectImpl["WAKEUP_REASON_EXT0"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_EXT0);
  systemClass->implement.objectImpl["WAKEUP_REASON_EXT1"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_EXT1);
  systemClass->implement.objectImpl["WAKEUP_REASON_TIMER"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_TIMER);
  systemClass->implement.objectImpl["WAKEUP_REASON_TOUCHPAD"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_TOUCHPAD);
  systemClass->implement.objectImpl["WAKEUP_REASON_ULP"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_ULP);
  systemClass->implement.objectImpl["WAKEUP_REASON_GPIO"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_GPIO);
  systemClass->implement.objectImpl["WAKEUP_REASON_UART"] =
      CreateNumberVariable((double)ESP_SLEEP_WAKEUP_UART);

  // 注册到全局变量
  std::string className = "System";
  auto ref = CreateReferenceVariable(systemClass);
  VMInstance->storeGlobalSymbol(className, ref);
}

void ESP32_PlatformStdlibImpl_Init(VM* VMInstance) {
  ESP32_SystemApi_Init(VMInstance);
  ESP32_GpioClass_Init(VMInstance);
  ESP32_FSAPI_Init(VMInstance);

#if ESP32_I2C_ENABLED
  ESP32_I2C_Init(VMInstance);
#endif

#if ESP32_SPI_ENABLED
  ESP32_SPI_Init(VMInstance);
#endif

#if ESP32_SERIALAPI_ENABLED
  ESP32_Serial_Init(VMInstance);
#endif

#if ESP32_WIFI_API_ENABLED
  ESP32_WiFiPlatformApi_Init(VMInstance);
#endif
}

void ESP32Driver_Init(VM* VMInstance) {
  // 暂时禁用GC避免创建对象中途发生GC导致垂悬指针
  VMInstance->currentGC->GCDisabled = true;
  ESP32_PlatformStdlibImpl_Init(VMInstance);
  VMInstance->currentGC->GCDisabled = false;
}

#endif