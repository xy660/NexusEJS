#include "VM.h"
#include "VariableValue.h"
#include <memory.h>
#include <cstring>
#include <vector>

//此方法不保证线程安全，请在VM初始化前安全调用或加锁调用
bool VM::LoadPackedProgram(uint8_t* data, uint32_t length) {

	uint8_t magic[] = { 0x78, 0x79, 0x78, 0x79 };

	for (int i = 0; i < 4; i++) { //验证magic
		if (data[i] != magic[i]) {
			return false;
		}
	}

	uint32_t pos = 4;

	std::vector<VMObject*> ConstStringPool; //字符串对象常量池

	uint32_t stringPoolSize;
	//= *(uint32_t*)(data + pos);
	memcpy(&stringPoolSize, data + pos, sizeof(uint32_t));
	pos += sizeof(uint32_t);
	for (int i = 0; i < stringPoolSize; i++) {
		uint32_t length;
		//= *(uint32_t*)(data + pos);
		memcpy(&length, data + pos, sizeof(uint32_t));
		pos += sizeof(uint32_t);
		VMObject* vmo = (VMObject*)platform.MemoryAlloc(sizeof(VMObject));
		new (vmo) VMObject(ValueType::STRING);
		vmo->flag_isLocalObject = true; //常量标志，表示对象不受GC管理
		vmo->implement.stringImpl.resize(length);
		memcpy(&vmo->implement.stringImpl[0], data + pos, length * sizeof(wchar_t));
		//vmo->implement.stringImpl = std::wstring((wchar_t*)(data + pos), length);
		ConstStringPool.push_back(vmo);
		pos += sizeof(wchar_t) * length;
	}

	this->ConstObjectPools.push_back(ConstStringPool);
	uint32_t constStringPoolId = this->ConstObjectPools.size() - 1;


	while (pos < length) {

		uint16_t fnNameLength;
		//= *(uint16_t*)(data + pos);
		memcpy(&fnNameLength, data + pos, sizeof(uint16_t));
		pos += sizeof(uint16_t);
		//std::wstring fnName((wchar_t*)(data + pos),fnNameLength);
		std::wstring fnName;
		fnName.resize(fnNameLength);
		memcpy(&fnName[0], data + pos, fnNameLength * sizeof(wchar_t));
		pos += sizeof(wchar_t) * fnNameLength;

		uint8_t argumentCount = *(data + pos); //本身1字节不需要拷贝
		pos += sizeof(uint8_t);
		std::vector<std::wstring> arguments;
		for (int i = 0; i < argumentCount; i++) {
			uint16_t argumentNameLength;
			//= *(uint16_t*)(data + pos);
			memcpy(&argumentNameLength, data + pos, sizeof(uint16_t));
			pos += sizeof(uint16_t);

			std::wstring argumentName;
			argumentName.resize(argumentNameLength);
			memcpy(&argumentName[0], data + pos, argumentNameLength * sizeof(wchar_t));
			arguments.push_back(argumentName);
			pos += sizeof(wchar_t) * argumentNameLength;
		}
		uint32_t byteCodeLength;
		//= *(uint32_t*)(data + pos);
		memcpy(&byteCodeLength, data + pos, sizeof(uint32_t));
		pos += sizeof(uint32_t);
		uint8_t* byteCodeBuffer = (uint8_t*)platform.MemoryAlloc(byteCodeLength);
		memcpy(byteCodeBuffer, data + pos, byteCodeLength);
		pos += byteCodeLength;

		//创建字节码元数据存储实例，生命周期和VM相同
		VariableValue fn;
		fn.varType = ValueType::FUNCTION;
		fn.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
		new (fn.content.function) ScriptFunction(ScriptFunction::Local);
		this->ScriptFunctionObjects.push_back(fn.content.function); //创建存储到全局方法单例表
		fn.content.function->argumentCount = arguments.size();
		fn.content.function->funcImpl.local_func.byteCode = byteCodeBuffer;
		fn.content.function->funcImpl.local_func.byteCodeLength = byteCodeLength;
		fn.content.function->funcImpl.local_func.funcName = fnName;
		fn.content.function->funcImpl.local_func.arguments = arguments;
		fn.content.function->funcImpl.local_func.constStringPoolId = constStringPoolId;
		this->storeGlobalSymbol(fnName, fn);
	}

	return true;
}

void VM::UnloadAllPackage() {
	//释放存储的固定脚本方法对象
	for (auto pScriptFunc : ScriptFunctionObjects) {
		platform.MemoryFree(pScriptFunc);
	}
	//释放所有常量池的内存
	for (auto& objPool : ConstObjectPools) {
		for (auto pConstObject : objPool) {
			platform.MemoryFree(pConstObject);
		}
	}
}

VariableValue VM::InitAndCallEntry(std::wstring& name) {
	platform.MutexLock(this->globalSymbolLock);
	this->workers.push_back(std::unique_ptr<VMWorker>(new VMWorker(this)));
	VMWorker* worker = this->workers.back().get();
	platform.MutexUnlock(this->globalSymbolLock);

	//getGlobalSymbol函数本身自带锁，避免死锁
	VariableValue* entryRef = getGlobalSymbol(name);

	platform.MutexLock(this->globalSymbolLock);
	if (entryRef->varType != ValueType::FUNCTION) {
		this->workers.pop_back();
		return VariableValue();
	}
	ByteCodeFunction& entry = entryRef->content.function->funcImpl.local_func;
	//TaskContext context;
	//context.id = this->lastestTaskId++;
	//context.

	platform.MutexUnlock(this->globalSymbolLock);
	return worker->Init(entry);

}

VariableValue VM::InvokeCallback(ByteCodeFunction& code, std::vector<VariableValue>& args)
{
	//加锁
	platform.MutexLock(this->globalSymbolLock);
	this->workers.push_back(std::unique_ptr<VMWorker>(new VMWorker(this)));
	VMWorker* worker = this->workers.back().get();
	platform.MutexUnlock(this->globalSymbolLock);
	if (args.size() != code.arguments.size()) {
		return VariableValue();
	}
	std::unordered_map<std::wstring, VariableValue> argumentMap;
	for (int i = 0; i < args.size(); i++) {
		argumentMap[code.arguments[i]] = args[i];
	}

	return worker->Init(code, &argumentMap);
}
