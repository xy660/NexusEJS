#pragma once

#include "GC.h"
#include "VariableValue.h"
#include "VM.h"
#include <unordered_map> 
#include <string>
#include <vector>

void ArrayManager_Init();
void ArrayManager_Destroy();
VariableValue GetArraySymbol(std::string& symbol, VMObject* owner);

