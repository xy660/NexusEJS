#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include "VariableValue.h"
#include "VM.h" 
#include "GC.h"
#include "PlatformImpl.h"

struct ByteBufferInfo
{
	uint8_t* data;
	uint32_t length;
};

ByteBufferInfo GetByteBufferInfo(uint32_t bufid);

VMObject* CreateTaskControlObject(uint32_t id, uint32_t threadId, VMWorker* worker);

// 初始化全局单例
void BuildinStdlib_Init();

void BuildinStdlib_Destroy();

VMObject* CreateByteBufferObject(uint32_t size, VMWorker* worker);

void RegisterSystemFunc(std::string name, uint8_t argCount, SystemFuncDef implement);


VariableValue* SystemGetSymbol(std::string& symbol);