#include <stack> 
#include <vector>
#include <unordered_map>
#include <string>
#include <cstring>
#include <cmath>
#include <stdint.h>
#include <iostream>
#include <memory>
#include "VariableValue.h"
#include "PlatformImpl.h"
#include "ByteCode.h"
#include "GC.h"
#include "ArrayMapager.h"
#include "ObjectManager.h"
#include "BuildinStdlib.h"
#include "StringConverter.h"
#include "StringManager.h"

#if VM_DEBUGGER_ENABLED
#include "VMDebugger.h"
#endif



//存储系统函数全局单例
std::vector<ScriptFunction*> VM::SystemFunctionObjects;

//全局单例初始化，static类型
bool single_instance_man_inited = false;
void VM::InitSingleInstanceManager() {
	if (single_instance_man_inited) {
		return;
	}
	single_instance_man_inited = true;

	ArrayManager_Init();
	StringManager_Init();
	ObjectManager_Init();
	BuildinStdlib_Init();
}

void VM::DestroySingleInstanceManager() {
	if (!single_instance_man_inited) {
		return;
	}
	single_instance_man_inited = false;

	ArrayManager_Destroy();
	StringManager_Destroy();
	ObjectManager_Destroy();
	BuildinStdlib_Destroy();
}

void VM::CleanUp()
{
	VM::DestroySingleInstanceManager();
}

VM::VM(GC* gc) {
	//初始化全局单例的管理器
	InitSingleInstanceManager();


	//初始化自身
	currentGC = gc;
	currentGC->GCInit(this);
	globalSymbolLock = platform.MutexCreate();

	//内置全局对象，上面查找需要用到，然后后面文档提一下不要乱改global对象类型
	//后续VariableValue内置一个readonly属性吧，防止开发者乱改
	auto globalObject = currentGC->Internal_NewObject(ValueType::OBJECT);
	auto globalObjectRef = CreateReferenceVariable(globalObject);
	globalObjectRef.readOnly = true;
	std::string globalObjectName = "global";
	storeGlobalSymbol(globalObjectName, globalObjectRef);

	std::string undef_name = "undefined";
	VariableValue undefined;
	undefined.readOnly = true; //阻止用户重新赋值
	storeGlobalSymbol(undef_name, undefined);

}

//析构所有资源：方法单例+全局符号表+最后一次GC 
//请在GC析构之前析构VM
VM::~VM()
{
	platform.MutexDestroy(globalSymbolLock);

	/*
	//清理掉加载进来的字节码方法
	for (ScriptFunction* fn : ScriptFunctionObjects) {
		fn->~ScriptFunction();
		platform.MemoryFree(fn);
	}
	*/

	globalSymbols.clear();
	for (auto worker : workers) {
		worker->~VMWorker();
		platform.MemoryFree(worker);
	}
	workers.clear();
	tasks.clear();
	UnloadAllPackage();
	currentGC->GC_Collect();

}

VariableValue* VM::getGlobalSymbol(std::string& symbol) {
	platform.MutexLock(globalSymbolLock);
	VariableValue* ret = NULL;
	auto it = globalSymbols.find(symbol);
	if (it != globalSymbols.end()) {
		ret = &(*it).second;
	}
	platform.MutexUnlock(globalSymbolLock);
	return ret;
}
void VM::storeGlobalSymbol(std::string& symbol, VariableValue& value) {
	platform.MutexLock(globalSymbolLock);
	globalSymbols[symbol] = value;
	platform.MutexUnlock(globalSymbolLock);
}

VariableValue VM::CreateSystemFunc(uint8_t argCount, SystemFuncDef implement) {
	ScriptFunction* sfn = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (sfn) ScriptFunction(ScriptFunction::System);
	VM::SystemFunctionObjects.push_back(sfn);
	sfn->argumentCount = argCount;
	sfn->funcImpl.system_func = implement;
	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = sfn;
	fnref.readOnly = true; //内置系统方法不允许修改
	return fnref;
}



void VM::VM_UnhandledException(VMObject* exceptionObject, VMWorker* worker)
{
	//todo
	//当前worker未处理异常触发此

	//std::cout << "\r\nUnhandled Exception : " << << exceptionObject->ToString().c_str() << "\r\n";

	std::string errorString = exceptionObject->ToString();

	printf("\r\nUnhandled Exception %s Process Waitting...\r\n", errorString.c_str());

	worker->getCurrentCallingLink().clear(); //终止当前VThread
}


