#include "ArrayMapager.h"
#include "PlatformImpl.h"
#include <stdint.h>

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

std::unordered_map<std::wstring, VariableValue> array_symbol_map;
std::vector<ScriptFunction*> arrayman_script_function_alloc;

void* ArraySymbolMapLock;

void ArraySymbolFuncAdd(std::wstring name,SystemFuncDef func,uint16_t argumentCount) {
	
	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (fnref.content.function) ScriptFunction(ScriptFunction::System);
	arrayman_script_function_alloc.push_back(fnref.content.function);
	fnref.content.function->argumentCount = argumentCount;
	fnref.content.function->funcImpl.system_func = func;
	array_symbol_map[name] = fnref;
	
}

void ArrayManager_Init() { 

	ArraySymbolMapLock = platform.MutexCreate();

	ArraySymbolFuncAdd(L"push", [](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue {
		if (thisValue->type != ValueType::ARRAY) {
			return VariableValue();
		}

		//复制传入的值加入容器实现持久化
		thisValue->implement.arrayImpl.push_back(*args[0].getRawVariable());

		VariableValue ret;
		ret.varType = ValueType::REF;
		ret.content.ref = thisValue;
		return ret;
		},1);

	//STORE存储会丢失Bridge被归一化到最终值类型进行赋值，因此这个bridge仅在arr[i] = xxx这一条有效
	ArraySymbolFuncAdd(L"get", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {
		if (thisValue->type != ValueType::ARRAY || args[0].getContentType() != ValueType::NUM) {
			return VariableValue();
		}

		uint32_t index = (uint32_t)args[0].content.number;

		if (index < 0 || index >= thisValue->implement.arrayImpl.size()) {
			return VariableValue();
		}

		VariableValue ret;
		ret.varType = ValueType::BRIDGE;
		ret.content.bridge_ref = &thisValue->implement.arrayImpl[index];
		return ret;
		},1);


}

void ArrayManager_Destroy() {
	platform.MutexDestroy(ArraySymbolMapLock);

	for (auto& pair : array_symbol_map) {
		if (pair.second.varType == ValueType::FUNCTION) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
	}

	array_symbol_map.clear();
}

VariableValue GetArraySymbol(std::wstring& symbol,VMObject* owner) {
	//对动态字段的特殊处理
	if (symbol == L"length") {
		VariableValue ret;
		ret.varType = ValueType::NUM;
		ret.content.number = owner->implement.arrayImpl.size();
		return ret;
	}

	platform.MutexLock(ArraySymbolMapLock);

	if (array_symbol_map.find(symbol) == array_symbol_map.end()) {
		platform.MutexUnlock(ArraySymbolMapLock);
		return VariableValue(); //如果不存在就返回NULLREF，避免破坏
	}
	
	//复制并设置thisValue
	VariableValue ret = array_symbol_map[symbol];
	ret.thisValue = owner;
	platform.MutexUnlock(ArraySymbolMapLock);
	return ret;
}