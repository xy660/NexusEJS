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



#include "ESP8266Driver/ESP8266Driver.h"


void CreatePWMObject(){
    //todo
    //后续需要适配
}

void setup()
{
    ESP8266_Platform_Init();
    Serial.println("try construct");
    wchar_t buf[32];
    wchar_t* str = L"hello";
    uint8_t* notalign = (uint8_t*)buf;
    notalign += 1;
    memcpy(notalign,str,sizeof(wchar_t) * 5);

    std::wstring a((wchar_t*)notalign,5);
    Serial.println("ok");

    Serial.printf("%ls\n",a.c_str());
    

    Serial.println("platform inited");

    RegisterSystemFunc(L"println", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {
    Serial.print("print=>");
    Serial.println(wstring_to_string(args[0].ToString()).c_str());
    //wprintf(L"傻逼");
    //std::wcout << args[0].ToString() << std::endl;
    VariableValue ret;
    ret.varType = ValueType::NUM;
    ret.content.number = 114514; //测试C++系统函数返回值
    return ret; 
});

    RegisterSystemFunc(L"delay", 1, [](std::vector<VariableValue> &args, VMObject *thisValue, VMWorker *currentWorker) -> VariableValue
    {
    
    delay((uint32_t)args[0].content.number);

    return VariableValue(); 
    });
    
    RegisterSystemFunc(L"gc", 0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
    
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

    uint8_t packed[] = {120,121,120,121,16,0,0,0,3,0,0,0,97,0,114,0,114,0,4,0,0,0,112,0,117,0,115,0,104,0,1,0,0,0,105,0,1,0,0,0,116,0,3,0,0,0,103,0,101,0,116,0,7,0,0,0,112,0,114,0,105,0,110,0,116,0,108,0,110,0,2,0,0,0,105,0,61,0,12,0,0,0,102,0,105,0,98,0,32,0,116,0,101,0,115,0,116,0,32,0,102,0,105,0,98,0,3,0,0,0,102,0,105,0,98,0,5,0,0,0,108,0,105,0,103,0,104,0,116,0,3,0,0,0,99,0,110,0,116,0,4,0,0,0,71,0,112,0,105,0,111,0,3,0,0,0,115,0,101,0,116,0,5,0,0,0,100,0,101,0,108,0,97,0,121,0,2,0,0,0,103,0,99,0,9,0,0,0,103,0,99,0,32,0,99,0,97,0,108,0,108,0,101,0,100,0,3,0,102,0,105,0,98,0,1,1,0,116,0,215,0,0,0,21,209,0,0,0,0,27,0,0,42,27,0,0,45,39,30,27,1,0,46,30,25,0,0,0,0,0,0,240,63,33,1,24,25,0,0,0,0,0,0,240,63,33,1,24,40,24,21,137,0,0,0,0,27,2,0,42,27,2,0,45,25,0,0,0,0,0,0,0,64,40,24,21,112,0,0,0,1,27,2,0,45,27,3,0,45,17,32,98,0,0,0,21,60,0,0,0,0,27,0,0,45,27,1,0,46,27,0,0,45,27,4,0,46,27,2,0,45,25,0,0,0,0,0,0,240,63,1,33,1,27,0,0,45,27,4,0,46,27,2,0,45,25,0,0,0,0,0,0,0,64,1,33,1,0,33,1,24,27,2,0,45,30,25,0,0,0,0,0,0,240,63,0,40,25,0,0,0,0,0,0,240,63,1,24,31,144,255,255,255,27,0,0,45,27,4,0,46,27,3,0,45,25,0,0,0,0,0,0,240,63,1,33,1,34,10,0,109,0,97,0,105,0,110,0,95,0,101,0,110,0,116,0,114,0,121,0,0,43,1,0,0,21,37,1,0,0,0,21,108,0,0,0,0,27,2,0,42,27,2,0,45,25,0,0,0,0,0,0,20,64,40,24,21,83,0,0,0,1,27,2,0,45,25,0,0,0,0,0,0,73,64,15,32,64,0,0,0,21,36,0,0,0,0,27,5,0,45,27,6,0,27,2,0,45,0,33,1,24,27,5,0,45,27,7,0,27,8,0,45,27,2,0,45,33,1,0,33,1,24,27,2,0,45,30,25,0,0,0,0,0,0,20,64,0,40,24,31,173,255,255,255,27,9,0,42,27,9,0,45,28,0,40,24,27,10,0,42,27,10,0,45,25,0,0,0,0,0,0,0,0,40,24,21,142,0,0,0,1,28,1,32,135,0,0,0,27,9,0,45,27,9,0,45,5,40,24,27,11,0,45,27,12,0,46,27,9,0,45,25,0,0,0,0,0,0,0,64,33,2,24,27,13,0,45,25,0,0,0,0,0,64,127,64,33,1,24,27,10,0,45,30,25,0,0,0,0,0,0,240,63,0,40,25,0,0,0,0,0,0,240,63,1,24,27,10,0,45,25,0,0,0,0,0,0,36,64,4,25,0,0,0,0,0,0,0,0,13,32,23,0,0,0,21,17,0,0,0,0,27,14,0,45,33,0,24,27,5,0,45,27,15,0,33,1,24,31,114,255,255,255};
    Serial.println("startting");
    vm.LoadPackedProgram(packed,sizeof(packed));
    Serial.println("loaded funcs");

    for(auto& func : vm.globalSymbols){
        Serial.println(wstring_to_string(func.first).c_str());
    }

    std::atomic<int> atom;

    std::wstring name = L"main_entry";
    uint32_t start = millis();
    auto ret = vm.InitAndCallEntry(name);

    Serial.printf("time => %d\n",millis() - start);
    Serial.print("return => ");
    Serial.printf("%s\n",wstring_to_string(ret.ToString()).c_str());

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