#include "ObjectManager.h"

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

std::unordered_map<std::string, VariableValue> object_buildin_symbol_map;
std::vector<ScriptFunction*> objectman_script_function_alloc;

void ObjectSymbolFuncAdd(std::string name, SystemFuncDef func, uint16_t argumentCount) {
	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (fnref.content.function) ScriptFunction(ScriptFunction::System);
	objectman_script_function_alloc.push_back(fnref.content.function);
	fnref.content.function->argumentCount = argumentCount;
	fnref.content.function->funcImpl.system_func = func;
	object_buildin_symbol_map[name] = fnref;
}

VariableValue GetObjectBuildinSymbol(std::string& symbol, VMObject* owner) {
	if (object_buildin_symbol_map.find(symbol) == object_buildin_symbol_map.end()) {
		return VariableValue(); //如果不存在就返回NULLREF，避免破坏
	}
	VariableValue ret = object_buildin_symbol_map[symbol];
	ret.thisValue = owner;
	return ret;
}

void ObjectManager_Init()
{
	//SymbolFuncAdd("keys")
	ObjectSymbolFuncAdd("get", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (thisValue->type != ValueType::OBJECT) {
			return VariableValue();
		}

		//传入参数全部转字符串作为key查找
		VariableValue ret;
		ret.varType = ValueType::BRIDGE;
		ret.content.bridge_ref = &thisValue->implement.objectImpl[args[0].ToString()];
		ret.thisValue = thisValue;
		return ret;

	}, 1);
} 

void ObjectManager_Destroy() {
	for (auto& pair : object_buildin_symbol_map) {
		if (pair.second.varType == ValueType::FUNCTION) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
	}

	object_buildin_symbol_map.clear();
}
