#ifdef ESP32_PLATFORM

#include "VM.h"
#include "GC.h"
#include <Arduino.h>

#include "ESP32Driver/Esp32Driver.h"

#if ESP32_PWM_ENALBED

uint8_t pwmChannelObjectsBuffer[sizeof(VMObject) * MAX_PWM_CHANNELS];

VMObject* pwmChannelObjects = (VMObject*)pwmChannelObjectsBuffer;

void InitPWMChannels(){
    for(int i = 0;i < MAX_PWM_CHANNELS;i++){
        new (&pwmChannelObjects[i]) VMObject(ValueType::OBJECT);
        pwmChannelObjects[i].implement.objectImpl["channe"] = CreateNumberVariable(i);
        pwmChannelObjects[i].implement.objectImpl["frequency"] = CreateNumberVariable(1000);
        pwmChannelObjects[i].implement.objectImpl["resolution"] = CreateNumberVariable(10);
        //PWMObject.attach(pin);
        pwmChannelObjects[i].implement.objectImpl["attach"] = VM::CreateSystemFunc(1,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
            if(args[0].getContentType() != ValueType::NUM){
                currentWorker->ThrowError("PWM.attach only accept number");
                return VariableValue();
            }
            uint32_t freq = thisValue->implement.objectImpl["frequency"].content.number;
            uint32_t res = thisValue->implement.objectImpl["resolution"].content.number;
            uint32_t channel = thisValue->implement.objectImpl["channe"].content.number;
            uint8_t pin = args[0].content.number;
            ledcSetup(channel,freq,res);
            ledcAttachPin(pin,channel);
            return VariableValue();
        });
        //PWMObject.setDuty(duty);
        pwmChannelObjects[i].implement.objectImpl["setDuty"] = VM::CreateSystemFunc(1,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
            if(args[0].getContentType() != ValueType::NUM){
                currentWorker->ThrowError("setDuty only accept number");
                return VariableValue();
            }
            uint32_t channel = thisValue->implement.objectImpl["channe"].content.number;
            ledcWrite(channel,(uint32_t)args[0].content.number);
            return VariableValue();
        });
    }
}

#endif

#endif