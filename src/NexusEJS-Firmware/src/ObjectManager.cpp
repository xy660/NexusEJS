#include "ObjectManager.h"

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

#define PROTO_FIND_MAX_ITER_CNT 10

//所有对象根原型（自身没有原型，原型是null）
VMObject objectPrototype(ValueType::OBJECT,nullptr);

std::unordered_map<std::string, VariableValue>& object_buildin_symbol_map = objectPrototype.implement.objectImpl;
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

//返回值：true=查找到已有的值，false表示未找到/创建新的值（isAssign=true才会创建新值）
bool GetObjectField(std::string& key, VMObject* parent,VariableValue& result,bool isAssign) {
	//返回BRIDGE类型，确保修改对象可以修改obj.member

	auto& obj_container = parent->implement.objectImpl;
	
	//map隐式创建可key，这个Bridge指针生命周期只在这条表达式内
	//优先查找对象符号表，找不到就看看内置符号表有没有，如果没有就创建有就返回
	//查找优先级 对象符号表>对象原型>内置绑定
	if (obj_container.find(key) != obj_container.end()) {
		//从符号表取出的拷贝引用设置thisValue绑定
		result.varType = ValueType::BRIDGE;
		result.content.bridge_ref = &obj_container[key];
		result.content.bridge_ref->thisValue = parent;
		return true;
	}
	else {

		if (!isAssign) {
			//查找原型链
			VMObject* cur = parent;
			
			auto* cur_proto = cur->prototype;

			int depth = 0;
			while (cur_proto) {
				//原型必须是OBJECT才能读取property
				if (cur_proto->type != ValueType::OBJECT) break;

				auto proto_find = cur_proto->implement.objectImpl.find(key);
				if (proto_find != cur_proto->implement.objectImpl.end()) {
					//原型查找只读，不可赋值
					result = (*proto_find).second;
					result.thisValue = parent;
					return true;
				}

				depth++;
				if (depth >= PROTO_FIND_MAX_ITER_CNT) break;

				cur = cur_proto;
				cur_proto = cur->prototype;
			}

		}

		//创建默认值(注意这里创建的是null，初始化对象是VM解释器循环的GET_FIELD_ASS那一刻负责的)
		if (isAssign) {
			result.varType = ValueType::BRIDGE;
			result.content.bridge_ref = &obj_container[key];
			result.content.bridge_ref->thisValue = parent;
		}
		else {
			result.varType = ValueType::UNDEFINED;
		}

		return false;
	}

	return true;
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

	ObjectSymbolFuncAdd("toString", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		
		auto* str = currentWorker->VMInstance->currentGC->GC_NewStringObject(thisValue->ToString());
		return CreateReferenceVariable(str);

		}, 0);

	ObjectSymbolFuncAdd("hasOwnProperty",[](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {

		VMObject* targetObj = thisValue;

		// 拦截 Array / String：无自有属性表，不支持自定义属性查询
		if (targetObj->type == ValueType::ARRAY || targetObj->type == ValueType::STRING) {
			worker->ThrowError("hasOwnProperty: Array and String instances do not support custom properties");
			return VariableValue();
		}


		if (targetObj->type == ValueType::FUNCTION) {
			targetObj = targetObj->implement.closFuncImpl.propObject;
		}

		//仅查询自身自有属性map，不遍历原型链
		bool exist = targetObj->implement.objectImpl.find(args[0].ToString()) != targetObj->implement.objectImpl.end();

		return CreateBooleanVariable(exist);
		},1);
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
