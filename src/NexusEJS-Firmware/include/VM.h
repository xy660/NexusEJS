#pragma once

#define VM_VERSION_NUMBER 5

#define VM_VERSION_STR "V1.5.0"

#define DYNAMIC_ARGUMENT 0xFF

#define VM_DEBUGGER_ENABLED 0

#define VT_SCHED_INSTRUCTION_CNT 100
#define VT_SCHED_IDLE_SLEEP_TIME_MS 10

#include <memory>
#include <vector>
#include <unordered_map>
#include "ByteCode.h"
#include "VariableValue.h"



class GC;
class VM;
class VMWorker;
class ScopeFrame;
class VirtualThreadSchedBlock;

static bool vtCanScheduleCheck(VirtualThreadSchedBlock& block);


//方法级别独立执行上下文（栈帧）
class FuncFrame {
public:
	uint8_t* byteCode = NULL; //字节码指针
	uint32_t byteCodeLength = 0; //字节码长度
	ByteCodeFunction* functionInfo;
	int sp = 0; //栈指针
	
	VariableValue returnValue;
	std::vector<VariableValue> virtualStack;
	std::vector<VariableValue> localVariables; //栈帧变量表
	std::vector<uint16_t> localVarNames; //名称映射
	std::vector<ScopeFrame> scopeStack; //作用域链
	std::unordered_map<std::string, VariableValue> functionEnvSymbols; //函数环境符号表
	//std::unordered_map<uint16_t, uint16_t> variableNameMapper; //<str_id,var_id>映射表


	FuncFrame() {
		//初始化默认值
		returnValue.varType = ValueType::NULLREF;
		functionInfo = NULL;
	}
};

class ScopeFrame {
public:
	int byteCodeStart = 0;
	int byteCodeLength = 0; //字节码范围长度
	int ep = 0; //作用域相对执行指针
	uint16_t spStart = 0; //进入作用域时栈的size，便于恢复
	uint16_t localvarStart = 0; //进入作用域时的变量栈索引size
	
	int exceptionHandlerEIP = 0; //异常处理程序在当前作用域的相对地址

	//当前作用域帧可接受的控制流指令响应
	
	enum ControlFlowType {
		NONE = 0,
		BREAK = 1 << 0,
		CONTINUE = 1 << 1,
		TRYCATCH = 1 << 2,
	};
	uint8_t ControlFlowFlag = 0;
	inline bool CheckControlFlowType(ControlFlowType type) {
		return this->ControlFlowFlag & type;
	}
	
	

	//作用域内的变量
	//std::unordered_map<std::string, VariableValue> scopeVariables;
	
};

class VirtualThreadSchedBlock {
public:

	VirtualThreadSchedBlock() {

	}

	VirtualThreadSchedBlock(VariableValue& function) {
		if (function.varType == ValueType::FUNCTION && 
			function.content.function->type == ScriptFunction::System) {
			printf("invaild VirtualThreadBlock argument");
			return;
		}

		if (function.getContentType() != ValueType::FUNCTION) {
			printf("invaild VirtualThreadBlock argument type");
			return;
		}
		
		ScriptFunction* sfn = NULL;
		VMObject* closure = NULL;
		if (function.varType == ValueType::REF) {
			sfn = function.content.ref->implement.closFuncImpl.sfn;
			closure = function.content.ref->implement.closFuncImpl.closure;
		}
		else {
			sfn = function.content.function;
		}
		ScopeFrame defaultScope;
		defaultScope.byteCodeStart = 0;
		defaultScope.byteCodeLength = sfn->funcImpl.local_func.byteCodeLength;
		FuncFrame frame;
		frame.byteCode = sfn->funcImpl.local_func.byteCode;
		frame.byteCodeLength = sfn->funcImpl.local_func.byteCodeLength;
		frame.functionInfo = &sfn->funcImpl.local_func;
		frame.scopeStack.push_back(defaultScope);
		if (closure) {
			frame.functionEnvSymbols["_clos"] = CreateReferenceVariable(closure);
		}

		this->callFrames.push_back(frame);
		this->vtStatus = VirtualThreadSchedBlock::ACTIVED;
	}

	std::vector<FuncFrame> callFrames;
	uint32_t awakeTime = 0; //由TickCount32决定
	enum VirtualThreadStatus {
		NONE,
		ACTIVED,
		BLOCKING,
		DEAD
	} vtStatus = NONE;
};

class VMWorker {
private:

	std::vector<VirtualThreadSchedBlock> VirtualThreads;

	uint32_t vtSchedIndex = 0; //调度器指针，表示当前是第几个虚拟线程执行

	uint32_t vtInstrCounter = VT_SCHED_INSTRUCTION_CNT;

	bool needResetLoop = false;
	
	bool vtScheduleEnabled = true;

public:

	bool keepAlive = false;

	VM* VMInstance;

	uint32_t currentWorkerId; //当前工作id

	VariableValue Init(ByteCodeFunction& entry_func, std::vector<VariableValue>& args, std::unordered_map <std::string, VariableValue>* env = NULL);

	void ThrowError(std::string messageString);
	void ThrowError(VariableValue& messageString);

	VMWorker(VM* current_vm);

	~VMWorker() {
		//先放着看看，后续看看实际运行行为
		for (auto& vt : VirtualThreads) {
			vt.callFrames.clear();
		}
	}

	void vtScheduleNext(); //让调度器切换到下一个虚拟线程(此函数还承担清理死亡虚拟线程的任务)

	bool deadCheck();

	inline std::vector<VirtualThreadSchedBlock>& getAllVTBlocks()
	{
		return this->VirtualThreads;
	}

	inline VirtualThreadSchedBlock& getCurrentVTBlock() {
		return this->VirtualThreads[this->vtSchedIndex];
	}

	inline std::vector<FuncFrame>& getCurrentCallingLink()
	{
		return this->VirtualThreads[this->vtSchedIndex].callFrames;
	}

	inline void setVTScheduleEnabled(bool value) {
		vtScheduleEnabled = value;
	}


	VariableValue VMWorkerTask();

};

class TaskContext {
public:
	//VariableValue result; //暂定为冗余，后续可能删除
	VMObject* TaskObject = NULL;
	uint32_t id = 0;
	uint32_t threadId = 0;
	VMWorker* worker = NULL;
	VariableValue function;
	ThreadEntry processEntry = NULL; //执行线程
	volatile enum TaskStatus {
		STOPED,
		RUNNING,
		
	}status = STOPED;
};

class VM {
public:
	//常量对象池（每一个方法绑定一个，多个方法可共享同一个）
	//std::vector<std::vector<VMObject*>> ConstObjectPools;

	std::unordered_map<uint16_t, PackageContext> loadedPackages;

	//VMWorker中包含了正在执行的函数，需要确保this指针不会以外失效
	std::vector<VMWorker*> workers; 

	static std::vector<ScriptFunction*> SystemFunctionObjects;

	GC* currentGC;
	//存储worker上下文
	uint32_t lastestTaskId = 2; //用于自增id
	void* globalSymbolLock;


	std::unordered_map<std::string, VariableValue> globalSymbols;
	std::unordered_map<uint32_t, TaskContext> tasks;
	

	VM(GC* gc);

	~VM();

	VariableValue* getGlobalSymbol(std::string& symbol);

	void storeGlobalSymbol(std::string& symbol, VariableValue& value);

	static VariableValue CreateSystemFunc(uint8_t argCount, SystemFuncDef implement);

	void VM_UnhandledException(VMObject* exceptionObject, VMWorker* worker);

	//void RegisterSystemFunc(std::string name, uint8_t argCount, SystemFuncDef implement);

	uint16_t LoadPackedProgram(uint8_t* data, uint32_t length);
	void UnloadAllPackage();
	void UnloadPackage(uint16_t id);
	//GC用的，使用迭代器卸载包，内联
	inline std::unordered_map<uint16_t, PackageContext>::iterator UnloadPackageWithIterator(std::unordered_map<uint16_t, PackageContext>::iterator it)
	{
		auto& package = (*it);
		printf("GC.UnloadPackage:[%d]%s\n", (*it).second.packageId, (*it).second.packageName.c_str());
		for (auto pConstObject : package.second.ConstStringPool) {
			pConstObject->~VMObject();
			platform.MemoryFree(pConstObject);
		}
		for (auto& pair : package.second.bytecodeFunctions) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
		return loadedPackages.erase(it);
	}
	VariableValue* GetBytecodeFunctionSymbol(uint16_t id, std::string& name);
	PackageContext* GetPackageByName(std::string& name);

	VariableValue InitAndCallEntry(std::string& name, uint16_t id);

	//此函数不会作为嵌套子worker使用，仅在作为真线程时使用
	VariableValue InvokeCallbackWithWorker(VMWorker* worker, VariableValue& function, std::vector<VariableValue>& args, VMObject* thisValue);

	//基于临时worker的回调
	VariableValue InvokeCallbackWithTempWorker(VMWorker* worker, VariableValue& function, std::vector<VariableValue>& args, VMObject* thisValue);

	//只能接收字节码函数，请勿传入原生函数
	VariableValue InvokeCallback(VariableValue& code, std::vector<VariableValue>& args,VMObject* thisValue);


	static void InitSingleInstanceManager();

	static void DestroySingleInstanceManager();

	static void CleanUp();
};