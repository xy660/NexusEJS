#pragma once

#include <memory>
#include <vector>
#include "ByteCode.h"
#include "VariableValue.h"

#define DYNAMIC_ARGUMENT 0xFF

class GC;
class VM;
class VMWorker;
class ScopeFrame;


//方法级别独立执行上下文（栈帧）
class FuncFrame {
public:
	uint8_t* byteCode = NULL; //字节码指针
	uint32_t byteCodeLength = 0; //字节码长度
	ByteCodeFunction* functionInfo;
	int sp = 0; //栈指针
	
	VariableValue returnValue;
	std::vector<VariableValue> virtualStack;
	std::vector<ScopeFrame> scopeStack; //作用域链
	//std::vector<VMObject> constStringPool; //常量字符串池

	//VariableValue& VirtualStackPop();
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
	int spStart = 0; //进入作用域时栈的size，便于恢复
	
	int exceptionHandlerEIP = 0; //异常处理程序在当前作用域的相对地址

	//当前作用域帧可接受的控制流指令响应
	
	enum ControlFlowType {
		NONE = 0, //不可用，继续向上查找
		LOOP = 1 << 0, //可接受break/continue
		TRYCATCH = 1 << 1, //可接受throw
	};
	uint8_t ControlFlowFlag = 0;
	inline bool CheckControlFlowType(ControlFlowType type) {
		return this->ControlFlowFlag & type;
	}
	
	

	//作用域内的变量
	std::unordered_map<std::wstring, VariableValue> scopeVariables;
	
};

class VMWorker {
private:
	std::vector<FuncFrame> callFrames;

	bool needResetLoop = false;
	
	FuncFrame* GetCurrentFrame();

public:

	uint32_t currentWorkerId; //当前工作id

	VariableValue Init(ByteCodeFunction& entry_func, std::unordered_map <std::wstring, VariableValue>* args = NULL);

	void ThrowError(std::wstring messageString);

	VM* VMInstance;

	VMWorker(VM* current_vm);

	std::vector<FuncFrame>& getCallingLink();

	VariableValue VMWorkerTask();

	void ThrowError(VariableValue& messageString);
};

class TaskContext {
public:
	VariableValue result;
	uint32_t id = 0;
	uint32_t threadId = 0;
	VMWorker* worker = NULL;
	ByteCodeFunction* sfn = NULL; //指向方法的指针（方法均为常量/持久化变量，不会失效）
	ThreadEntry processEntry = NULL; //执行线程
	volatile enum TaskStatus {
		STOPED,
		RUNNING,
		
	}status = STOPED;
};

class VM {
public:
	//常量对象池（每一个方法绑定一个，多个方法可共享同一个）
	std::vector<std::vector<VMObject*>> ConstObjectPools;

	//VMWorker中包含了正在执行的函数，需要确保this指针不会以外失效
	std::vector<std::unique_ptr<VMWorker>> workers; 

	std::vector<ScriptFunction*> ScriptFunctionObjects; //存储分配出来的ScriptFunction对象(目前只存Local类型的)

	GC* currentGC;
	//存储worker上下文
	uint32_t lastestTaskId = 1; //用于自增id
	void* globalSymbolLock;


	std::unordered_map<std::wstring, VariableValue> globalSymbols;
	std::unordered_map<uint32_t, TaskContext> tasks;
	

	VM(GC* gc);

	~VM();

	VariableValue* getGlobalSymbol(std::wstring& symbol);

	void storeGlobalSymbol(std::wstring& symbol, VariableValue& value);

	static VariableValue CreateSystemFunc(uint8_t argCount, SystemFuncDef implement);

	void VM_UnhandledException(VMObject* exceptionObject, VMWorker* worker);

	//void RegisterSystemFunc(std::wstring name, uint8_t argCount, SystemFuncDef implement);

	bool LoadPackedProgram(uint8_t* data, uint32_t length);
	void UnloadAllPackage();
	VariableValue InitAndCallEntry(std::wstring& name);

	VariableValue InvokeCallback(ByteCodeFunction& code, std::vector<VariableValue>& args);


	static void InitSingleInstanceManager();

	static void DestroySingleInstanceManager();
};