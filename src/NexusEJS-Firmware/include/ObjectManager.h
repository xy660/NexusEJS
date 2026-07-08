#pragma once

#include "GC.h"
#include "VariableValue.h"
#include "VM.h"
#include <unordered_map>
#include <string> 
#include <vector>

void ObjectManager_Init();
void ObjectManager_Destroy();
VariableValue GetObjectBuildinSymbol(std::string& symbol, VMObject* owner);
bool GetObjectField(std::string& key, VMObject* parent, VariableValue& result,bool isAssign);