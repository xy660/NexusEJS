#pragma once
#include "GC.h"
#include "VariableValue.h"
#include "VM.h"
#include <unordered_map> 
#include <string>
#include <vector>

void StringManager_Init();
void StringManager_Destroy();
VariableValue GetStringValSymbol(std::wstring& symbol, VMObject* owner);
