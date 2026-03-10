#ifdef ESP8266_PLATFORM

#include <Arduino.h>
#include <vector>
#include <csetjmp>
#include <string>
#include <locale>
#include <codecvt>
#include <iostream>
#include <atomic>
#include "VM.h"
#include "GC.h"
#include "VariableValue.h"
#include "PlatformImpl.h"
#include "BuildinStdlib.h"
#include "StringConverter.h"

#include <LittleFS.h>



#include "ESP8266Driver/Esp8266Driver.h"


void CreatePWMObject(){
    //todo
    //后续需要适配
}

void setup()
{
    ESP8266_Platform_Init();

    Serial.println("platform inited");

    RegisterSystemFunc("println", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {
    Serial.print("print=>");
    Serial.println(args[0].ToString().c_str());
    //wprintf(L"傻逼");
    //std::wcout << args[0].ToString() << std::endl;
    VariableValue ret;
    ret.varType = ValueType::NUM;
    ret.content.number = 114514; //测试C++系统函数返回值
    return ret; 
});

    RegisterSystemFunc("delay", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {
    
    delay((uint32_t)args[0].content.number);

    return VariableValue(); 
    });
    
    RegisterSystemFunc("gc", 0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
    
    VariableValue ret;
    Serial.printf("memory:%.1f %\n",(1.0-(float)ESP.getFreeHeap()/81920)*100);
    currentWorker->VMInstance->currentGC->GC_Collect();
    Serial.printf("finished\n");

    ret.varType = ValueType::BOOL;
    ret.content.boolean = true; //测试C++系统函数返回值
    return ret;
    });

    Serial.println("system func init");

    GC gc;
    VM vm(&gc);

    //ESP8266Driver_Init(&vm);
    ESP8266Driver_Init(&vm);

    Serial.println("driver inited");

    if(!LittleFS.begin()){
        Serial.println("LittleFS挂载失败");
        return;
    }

    File entryNejs = LittleFS.open("/entry.nejs","r");
    if(!entryNejs){
        Serial.println("Can not open the entry program");
        return;
    }

    uint8_t* packed = (uint8_t*)malloc(entryNejs.size());
    entryNejs.read(packed,entryNejs.size());
    Serial.printf("read success,size=%d\n",entryNejs.size());
    uint16_t packageId = vm.LoadPackedProgram(packed,entryNejs.size());

    free(packed);

    Serial.println("calling entrypoint...");


    std::string name = "main_entry";
    uint32_t start = millis();

    Serial.printf("package_id=%d\n",packageId);
    for(auto& func : vm.loadedPackages[packageId].bytecodeFunctions){
        Serial.printf("Func:%s\n",func.first.c_str());
    }
    Serial.println("===end===");
    auto ret = vm.InitAndCallEntry(name,packageId);

    Serial.printf("time => %d\n",millis() - start);
    Serial.print("return => ");
    Serial.printf("%s\n",ret.ToString().c_str());

    while(true){
        delay(100);
    }
}

void connect()
{
}

void loop()
{
}

#endif