#include "VM.h"
#include "GC.h"
#include "VariableValue.h"
#include "BuildinStdlib.h"
#include <memory.h>
#include <cstring>
#include <vector>


//字符大小，目前修改为UTF8所以是1字节
#define CHAR_SIZE 1

uint16_t packageIdSeed = 1;

//此方法不保证线程安全，请在VM初始化前安全调用或加锁调用
uint16_t VM::LoadPackedProgram(uint8_t* data, uint32_t length) {

	uint8_t magic[] = { 0x78, 0x79, 0x78, 0x79 };

	for (int i = 0; i < 4; i++) { //验证magic
		if (data[i] != magic[i]) {
			return false;
		}
	}

	uint32_t pos = 4;

	//nejs文件VM要求最低版本号
	uint16_t version = 0;
	memcpy(&version, data + pos, sizeof(uint16_t));
	pos += sizeof(uint16_t);

	if (version != VM_VERSION_NUMBER) {
		return 0; //不兼容当前版本
	}

	uint16_t packageNameLnegth = 0;
	memcpy(&packageNameLnegth, data + pos, sizeof(uint16_t));
	pos += sizeof(uint16_t);

	std::string packageName;
	packageName.resize(packageNameLnegth);
	memcpy(&packageName[0], data + pos, CHAR_SIZE * packageNameLnegth);
	pos += CHAR_SIZE * packageNameLnegth;


	//构造新的程序包上下文
	uint32_t packageId = packageIdSeed++;

	if (packageId == 0xFFFF) {
		printf("warning!! MAX packageId!the VM abort now");
		abort();
	}

	std::vector<VMObject*>& ConstStringPool = this->loadedPackages[packageId].ConstStringPool;//字符串对象常量池

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
		//vmo->flag_isLocalObject = true; //常量标志，表示对象不受GC管理
		vmo->implement.stringImpl.resize(length);
		memcpy(&vmo->implement.stringImpl[0], data + pos, length * CHAR_SIZE);
		//vmo->implement.stringImpl = std::string((char*)(data + pos), length);
		ConstStringPool.push_back(vmo);
		pos += CHAR_SIZE * length;
	}
	
	while (pos < length) {

		//读取函数名
		uint16_t fnNameStrId;
		memcpy(&fnNameStrId, data + pos, sizeof(uint16_t));
		pos += sizeof(uint16_t);
		std::string& fnName = ConstStringPool[fnNameStrId]->implement.stringImpl;

		uint8_t argumentCount = *(data + pos); //本身1字节不需要拷贝
		pos += sizeof(uint8_t);
		std::vector<uint16_t> arguments;
		for (int i = 0; i < argumentCount; i++) {
			//读取参数
			uint16_t argumentNameStrId;
			memcpy(&argumentNameStrId, data + pos, sizeof(uint16_t));
			pos += sizeof(uint16_t);

			//std::string& argumentName = ConstStringPool[argumentNameStrId]->implement.stringImpl;

			arguments.push_back(argumentNameStrId);
		}

		//读取外部符号依赖
		uint16_t outsizeSymCount = 0;
		memcpy(&outsizeSymCount, data + pos, sizeof(uint16_t));
		pos += sizeof(uint16_t);
		std::vector<uint16_t> outsideSymbols;
		outsideSymbols.reserve(outsizeSymCount);
		for (int i = 0; i < outsizeSymCount; i++) {
			uint16_t symbolStrId = 0;
			memcpy(&symbolStrId, data + pos, sizeof(uint16_t));
			pos += sizeof(uint16_t);
			outsideSymbols.push_back(symbolStrId);
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
		//这个注释掉的废弃，目前存储于包体上下文
		//this->ScriptFunctionObjects.push_back(fn.content.function); 
		fn.content.function->argumentCount = arguments.size();
		fn.content.function->funcImpl.local_func.byteCode = byteCodeBuffer;
		fn.content.function->funcImpl.local_func.byteCodeLength = byteCodeLength;
		fn.content.function->funcImpl.local_func.funcName = fnName;
		fn.content.function->funcImpl.local_func.funcNameStrId = fnNameStrId;
		fn.content.function->funcImpl.local_func.arguments = arguments;
		fn.content.function->funcImpl.local_func.outsideSymbols = outsideSymbols;
		fn.content.function->funcImpl.local_func.packageId = packageId;
		//this->storeGlobalSymbol(fnName, fn);
		this->loadedPackages[packageId].bytecodeFunctions[fnName] = fn;
		this->loadedPackages[packageId].packageName = packageName;
		this->loadedPackages[packageId].packageId = packageId;
	}

	return packageId;
}

void VM::UnloadAllPackage() {
	//释放所有加载的程序包内存
	for (auto& package : loadedPackages) {
		for (auto pConstObject : package.second.ConstStringPool) {
			pConstObject->~VMObject();
			platform.MemoryFree(pConstObject);
		}
		for (auto& pScriptFunction : package.second.bytecodeFunctions) {
			pScriptFunction.second.content.function->~ScriptFunction();
			platform.MemoryFree(pScriptFunction.second.content.function);
		}
	}
}

void VM::UnloadPackage(uint16_t id) {
	auto& package = loadedPackages[id];
	for (auto pConstObject : package.ConstStringPool) {
		pConstObject->~VMObject();
		platform.MemoryFree(pConstObject);
	}
	for (auto& pair : package.bytecodeFunctions) {
		pair.second.content.function->~ScriptFunction();
		platform.MemoryFree(pair.second.content.function);
	}
	loadedPackages.erase(id);
}



VariableValue* VM::GetBytecodeFunctionSymbol(uint16_t id, std::string& name) {
	platform.MutexLock(globalSymbolLock);
	auto& bytecodeFuncs = this->loadedPackages[id].bytecodeFunctions;
	auto bcfnFind = bytecodeFuncs.find(name);
	if (bcfnFind != bytecodeFuncs.end()) {
		platform.MutexUnlock(globalSymbolLock);
		return &(*bcfnFind).second;
	}
	platform.MutexUnlock(globalSymbolLock);
	return NULL;
}

PackageContext* VM::GetPackageByName(std::string& name) {
	for (auto& pair : loadedPackages) {
		if (pair.second.packageName == name) {
			return &pair.second;
		}
	}
	return NULL;
}

VariableValue VM::InitAndCallEntry(std::string& name,uint16_t id) {

	VMWorker* worker = (VMWorker*)platform.MemoryAlloc(sizeof(VMWorker));
	new (worker) VMWorker(this);

	platform.MutexLock(currentGC->GCWorkersVecLock);
	this->workers.push_back(worker); //注册并接收GC管理
	platform.MutexUnlock(currentGC->GCWorkersVecLock);

	if (loadedPackages.find(id) == loadedPackages.end()) {
		//无此id返回NULLREF
		return VariableValue();
	}
	PackageContext& package = loadedPackages[id];
	
	auto entryRefIt = package.bytecodeFunctions.find(name);
	VariableValue* entryRef = NULL;
	if (entryRefIt != package.bytecodeFunctions.end()) {
		entryRef = &(*entryRefIt).second;
	}
	
	if (entryRef->varType != ValueType::FUNCTION) {
		platform.MutexLock(currentGC->GCWorkersVecLock);
		this->workers.pop_back();
		platform.MutexUnlock(currentGC->GCWorkersVecLock);
		return VariableValue();
	}
	ByteCodeFunction& entry = entryRef->content.function->funcImpl.local_func;

	std::vector<VariableValue> args; //main函数无参数
	uint32_t current_workerid = lastestTaskId++;
	worker->currentWorkerId = current_workerid;
	TaskContext context;
	context.function = *entryRef;
	context.id = current_workerid;
	context.status = TaskContext::RUNNING;
	context.worker = worker;
	platform.MutexLock(currentGC->GCWorkersVecLock);
	tasks[current_workerid] = context; //注册到VM
	platform.MutexUnlock(currentGC->GCWorkersVecLock);
	return worker->Init(entry,args);

}

//调用这里需要确保外部创建好了线程控制对象，避免内存泄漏
VariableValue VM::InvokeCallbackWithWorker(VMWorker* worker,VariableValue& function, std::vector<VariableValue>& args, VMObject* thisValue) {
	ScriptFunction* code = NULL;
	if (function.varType == ValueType::REF && function.content.ref->type == ValueType::FUNCTION) {
		code = function.content.ref->implement.closFuncImpl.sfn;
	}
	else if (function.varType == ValueType::FUNCTION) {
		code = function.content.function;
	}
	else {
		return VariableValue(); //静默失败
	}

	if (code->type != ScriptFunction::Local) {
		return VariableValue();
	}
	//参数不相等也失败
	if (args.size() != code->funcImpl.local_func.arguments.size()) {
		return VariableValue();
	}
	//赋值下参数
	std::unordered_map<std::string, VariableValue> env;

	//如果是对象的那种函数他是带闭包的，需要设置一下闭包环境(前提closure不为NULL)
	if (function.varType == ValueType::REF &&
		function.content.ref->implement.closFuncImpl.closure) {
		env["_clos"] = CreateReferenceVariable(
			function.content.ref->implement.closFuncImpl.closure);
	}

	//可选this指针
	if (thisValue) {
		env["this"] = CreateReferenceVariable(thisValue);
	}

	//加锁
	platform.MutexLock(currentGC->GCWorkersVecLock);
	this->workers.push_back(worker);
	uint32_t current_workerid = lastestTaskId++;
	worker->currentWorkerId = current_workerid;
	TaskContext context;
	context.function = function;
	context.id = current_workerid;
	context.status = TaskContext::RUNNING;
	context.worker = worker;
	tasks[current_workerid] = context; //注册到VM

	platform.MutexUnlock(currentGC->GCWorkersVecLock);


	return worker->Init(code->funcImpl.local_func, args, &env);
}

VariableValue VM::InvokeCallbackWithTempWorker(VMWorker* worker,VariableValue& function, std::vector<VariableValue>& args, VMObject* thisValue) {
	ScriptFunction* code = NULL;
	if (function.varType == ValueType::REF && function.content.ref->type == ValueType::FUNCTION) {
		code = function.content.ref->implement.closFuncImpl.sfn;
	}
	else if (function.varType == ValueType::FUNCTION) {
		code = function.content.function;
	}
	else {
		return VariableValue(); //静默失败
	}

	if (code->type != ScriptFunction::Local) {
		return VariableValue();
	}
	//参数不相等也失败
	if (args.size() != code->funcImpl.local_func.arguments.size()) {
		return VariableValue();
	}
	//赋值下参数
	std::unordered_map<std::string, VariableValue> env;

	//如果是对象的那种函数他是带闭包的，需要设置一下闭包环境(前提closure不为NULL)
	if (function.varType == ValueType::REF &&
		function.content.ref->implement.closFuncImpl.closure) {
		env["_clos"] = CreateReferenceVariable(
			function.content.ref->implement.closFuncImpl.closure);
	}

	//可选this指针
	if (thisValue) {
		env["this"] = CreateReferenceVariable(thisValue);
	}

	

	uint32_t current_workerid = lastestTaskId++;
	worker->currentWorkerId = current_workerid;
	TaskContext context;
	context.function = function;
	context.id = current_workerid;
	context.status = TaskContext::RUNNING;
	context.worker = worker;
	//加锁
	platform.MutexLock(currentGC->GCWorkersVecLock);

	currentGC->IgnoreWorkerCount_Inc(); //增加忽略计数器，忽略此worker的安全点需求
	worker->keepAlive = true;
	this->workers.push_back(worker);
	//tasks[current_workerid] = context; //注册到VM

	platform.MutexUnlock(currentGC->GCWorkersVecLock);


	
	auto res = worker->Init(code->funcImpl.local_func, args, &env);

	//回到原生需要加保护位，避免被GC干掉
	if (res.varType == ValueType::REF) {
		res.content.ref->protectStatus = VMObject::PROTECTED;
		//currentGC->SetObjectProtect(res.content.ref, true);
	}

	platform.MutexLock(currentGC->GCWorkersVecLock); //手动清除插入的worker
	for (auto it = workers.begin(); it != workers.end(); it++) {
		if (*it == worker) {
			workers.erase(it);
			break;
		}
	}
	worker->keepAlive = false;
	currentGC->IgnoreWorkerCount_Dec(); //减少忽略计数器
	platform.MutexUnlock(currentGC->GCWorkersVecLock);

	

	return res;
}

VariableValue VM::InvokeCallback(VariableValue& function, std::vector<VariableValue>& args,VMObject* thisValue)
{
	VMWorker worker(this);
	return InvokeCallbackWithTempWorker(&worker, function, args, thisValue);
}
