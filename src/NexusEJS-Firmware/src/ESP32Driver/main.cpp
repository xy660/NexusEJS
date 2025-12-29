
#ifdef ESP32_PLATFORM
#include <Arduino.h>
#include <vector>
#include <csetjmp>
#include <string>
#include <locale>
#include <codecvt>
#include <iostream>
#include <SPIFFS.h>
#include "VM.h"
#include "GC.h"
#include "VariableValue.h"
#include "PlatformImpl.h"
#include "BuildinStdlib.h"
#include "StringConverter.h"


#include "ESP32Driver/Esp32Driver.h"


void setup()
{

    

    ESP32_Platform_Init();

    

    Serial.println("platform inited");

    RegisterSystemFunc("println", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {
    Serial.print("print=>");
    Serial.println(args[0].ToString().c_str());
    VariableValue ret;
    return ret; 
});

    RegisterSystemFunc("delay", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {

    currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Inc();
    
    vTaskDelay((uint32_t)args[0].content.number / portTICK_PERIOD_MS);

    currentWorker->VMInstance->currentGC->IgnoreWorkerCount_Dec();

    return VariableValue(); 
    });
    
    RegisterSystemFunc("gc", 0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
    
    VariableValue ret;
    Serial.printf("memory:%.1f %\n",(1.0-(float)ESP.getFreeHeap()/ESP.getHeapSize())*100);
    currentWorker->VMInstance->currentGC->GC_Collect();
    Serial.printf("finished\n");

    return VariableValue();
    });

    Serial.println("system func init");

    GC gc;
    VM vm(&gc);

    ESP32Driver_Init(&vm);
    
    fs::File entry = SPIFFS.open("/entry.nejs");
    printf("size=%d\n",entry.size());
    
    uint8_t* entryNejs = (uint8_t*)platform.MemoryAlloc(entry.size());
    entry.readBytes((char*)entryNejs,entry.size());

    uint16_t packageId = vm.LoadPackedProgram(entryNejs,entry.size());
    if(packageId == 0){
        Serial.println("fail to load the NEJS package");
        return;
    }
    Serial.println("loading success! calling entry..");

    std::string name = "main_entry";
    uint32_t start = millis();
    auto ret = vm.InitAndCallEntry(name,packageId);

    Serial.printf("time => %d\n",millis() - start);
    Serial.print("return => ");
    Serial.printf("%s\n",ret.ToString().c_str());

    while(true);
}

void connect()
{
}

void loop()
{
}
#endif