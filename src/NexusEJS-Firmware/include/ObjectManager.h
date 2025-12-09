#pragma once

#include "GC.h"
#include "VariableValue.h"
#include "VM.h"
#include <unordered_map>
#include <string> 
#include <vector>

void ObjectManager_Init();
void ObjectManager_Destroy();
VariableValue GetObjectBuildinSymbol(std::wstring& symbol, VMObject* owner);