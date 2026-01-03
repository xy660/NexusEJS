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


#pragma region BINARY_OP_TEMPLATE

/*
		if (left->varType != ValueType::NUM || right->varType != ValueType::NUM) { \
			ThrowError(error_msg); \
		} \
*/
#define BINARY_NUMERIC_OP(syntax, error_msg) \
	{ \
		VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable(); \
		VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable(); \
		VariableValue res; \
		res.varType = ValueType::NUM; \
		res.content.number = syntax; \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.push_back(res); \
		break; \
	}

#define BINARY_BOOLEAN_OP(syntax, error_msg) \
	{ \
		VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable(); \
		VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable(); \
		VariableValue res; \
		res.varType = ValueType::BOOL; \
		res.content.boolean = syntax; \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.push_back(res); \
		break; \
	}

//数字运算返回布尔的运算符
#define BINARY_NUMERIC_OP_RETBOOL(syntax, error_msg) \
	{ \
		VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable(); \
		VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable(); \
		VariableValue res; \
		res.varType = ValueType::BOOL; \
		res.content.boolean = syntax; \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.pop_back(); \
		currentFn->virtualStack.push_back(res); \
		break; \
	}

#define BINARY_OP_LEFT_NUM left->content.number
#define BINARY_OP_RIGHT_NUM right->content.number
#define BINARY_OP_LEFT_BOOL left->Truty()
#define BINARY_OP_RIGHT_BOOL right->Truty()


#pragma endregion

#pragma region VM_WORKER

#ifdef _DEBUG

const char* IOpCodeStr[] = {
	//运算符
	"ADD",
	"SUB",
	"MUL",
	"DIV",
	"MOD",    // 取模

	// 单目运算符，弹出一个对象运算压回去
	"NOT",    // 逻辑非
	"NEG",    // 取负

	// 位运算符
	"BIT_AND",    // 按位与
	"BIT_OR",     // 按位或
	"BIT_XOR",    // 按位异或
	"BIT_NOT",    // 按位取反
	"SHL",        // 左移
	"SHR",         // 右移

	//逻辑运算符 弹出两个对象然后进行比较结果的BOOL对象压入栈
	"EQUAL",
	"NOT_EQUAL",
	"LOWER_EQUAL",
	"GREATER_EQUAL",
	"LOWER",
	"GREATER",
	"AND",
	"OR",

	//栈保护指令
	"SCOPE_PUSH", //创建新的作用域帧 带一个4字节操作数表示作用域字节码大小
	"BREAK", //强制弹出并退出作用域帧，类似break
	"CONTINUE", //重置当前作用域运行指针，类似continue
	"POP", //弹出并丢弃一个值
	//栈保护指令将当前栈指针压入一个独立的记录栈中用于恢复

	//逻辑操作
	"PUSH_NUM", //压入常量数字（NUM类型，原生类型为double）
	"PUSH_PTR", //压入PTR类型，原生类型long
	"PUSH_STR", //压入字符串，操作数：索引(ushort)
	"PUSH_BOOL", //压入布尔，操作数1字节，0或1
	"PUSH_NULL", //压入null到栈（如果需要JIT编译就等效PUSH_NUM 0）,无操作数
	"DUP_PUSH", //从栈顶拷贝一个VariableValue压入栈
	"JMP",
	"JMP_IF_FALSE",//从栈弹出2个对象，先出来的是地址，后出来的是条件
	"CALLFUNC",
	"RET",
	"TRY_ENTER", //标记try起点，压入异常处理程序栈（一个独立的TryCatch记录栈，异常发生后弹出异常处理程序栈最顶上的处理程序跳转）
	"TRY_END", //从异常处理栈弹出，标记try块结束，执行此指令后需要一个JMP跳过catch块
	"THROW",

	//变量管理
	"NEW_OBJ",
	"NEW_ARR",
	"STORE", //从栈弹出两个VariableValue，先后a，b，将a的VariableValue内的引用/值修改为b的值
	"STORE_LOCAL", //自带变量名索引，直接存入局部变量，带有一个2字节操作数表示字符串常量池索引
	"DEF_LOCAL", //弹出一个字符串，从局部符号表创建一个变量占位，默认值为NULL
	"LOAD_LOCAL",
	"DEL_DEF", //从栈弹出一个VariableValue，从全局符号表删除给定名称的值
	"LOAD_VAR",  //从栈弹出一个字符串，从全局/局部符号表寻找变量将值压入栈
	"GET_FIELD", //获取对象属性值，一个桥接VariableValue(type=BRIDGE)指向成员VariableValue的指针

	//常量池使用
	"CONST_STR", //字符串常量,Unicode表示;指令结构：（1byte头+4byte长度+内容）
};

#endif



std::vector<FuncFrame>& VMWorker::getCallingLink() {
	return VMWorker::callFrames;
}

FuncFrame* VMWorker::GetCurrentFrame() {

	if (callFrames.size() == 0) {
		return NULL;
	}
	return &callFrames[callFrames.size() - 1];
}

VMWorker::VMWorker(VM* current_vm) {
	VMInstance = current_vm;
}

//初始化操作，加载方法字节码帧并启动解释器循环
VariableValue VMWorker::Init(ByteCodeFunction& entry_func,std::vector<VariableValue>& args, std::unordered_map<std::string, VariableValue>* env) {
	FuncFrame frame;
	frame.byteCode = entry_func.byteCode;
	frame.byteCodeLength = entry_func.byteCodeLength;
	frame.functionInfo = &entry_func;
	ScopeFrame defaultScope;
	//初始化默认作用域
	defaultScope.byteCodeLength = entry_func.byteCodeLength;
	defaultScope.byteCodeStart = 0;
	defaultScope.ep = 0;
	defaultScope.spStart = 0;
	defaultScope.localvarStart = 0;
	if (env) {
		frame.functionEnvSymbols = *env; //拷贝环境哈希表到函数帧
	}
	uint32_t index = 0;
	for (auto& arg : args) {
		frame.localVariables.push_back(arg); //加入参数
		frame.localVarNames.push_back(entry_func.arguments[index]);
		index++;
	}
	frame.scopeStack.push_back(defaultScope);
	callFrames.push_back(frame);
	return VMWorkerTask();
}
void VMWorker::ThrowError(std::string messageString) {
	auto messageObject = VMInstance->currentGC->GC_NewStringObject(messageString);
	VariableValue message;
	message.varType = ValueType::REF;
	message.content.ref = messageObject;
	ThrowError(message);
}

void VMWorker::ThrowError(VariableValue& messageString)
{
	needResetLoop = true;

#ifdef _DEBUG

	//======调试用=======
		//system("cls");
	auto& currentFn = callFrames.back();
	auto& currentScope = currentFn.scopeStack.back();
	printf("Exception has thrown!");
	printf("Command: %s\r\n", IOpCodeStr[currentFn.byteCode[currentScope.byteCodeStart + currentScope.ep]]);
	printf("EIP: %d\r\n", currentScope.byteCodeStart + currentScope.ep);
	printf("===Environment===\r\n");
	for (auto& env_pair : currentFn.functionEnvSymbols) {
		printf("[%s]%s\n", env_pair.first.c_str(), env_pair.second.ToString().c_str());
	}
	printf("===Variables===\r\n");
	for (int i = 0; i < currentFn.localVariables.size(); i++) {

		uint16_t packageId = currentFn.functionInfo->packageId;
		auto& package = VMInstance->loadedPackages[packageId];
		auto& name = package.ConstStringPool[currentFn.localVarNames[i]]->implement.stringImpl;
		auto& varValue = currentFn.localVariables[i];

		printf("[%s]%s\n", name.c_str(), varValue.ToString().c_str());
	}
	printf("===Stack===\r\n");
	int stki = 0;
	for (auto& varb : currentFn.virtualStack) {
		stki++;
		printf("[%d]%s\r\n", stki, varb.ToString().c_str());
	}

	printf("\r\n\r\n===Scope===\r\n");

	for (auto& scope : currentFn.scopeStack) {
		printf("{start:%d ,length:%d ,ep:%d attr:%d }\r\n", scope.byteCodeStart, scope.byteCodeLength, scope.ep, scope.ControlFlowFlag);
	}



	//======调试用=======



#endif

	VMObject* errorObject = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
	errorObject->implement.objectImpl["message"] = messageString;

	std::ostringstream stream;
	stream << std::endl;
	for (int i = callFrames.size() - 1; i >= 0; i--) {
		auto& fnFrame = callFrames[i];
		stream << "at " << fnFrame.functionInfo->funcName << "(";
		for (int argIndex = 0; argIndex < fnFrame.functionInfo->arguments.size(); argIndex++) {
			if (argIndex != 0) stream << ",";
			auto argName = VMInstance->loadedPackages[fnFrame.functionInfo->packageId].ConstStringPool[fnFrame.functionInfo->arguments[argIndex]]->ToString();

			stream << argName;

			stream << "=";
			stream << fnFrame.localVariables[argIndex].ToString();
		}
		stream << ") offset:" << fnFrame.scopeStack.back().byteCodeStart + fnFrame.scopeStack.back().ep << std::endl;
	}

	errorObject->implement.objectImpl["stackTrace"] = CreateReferenceVariable(
		VMInstance->currentGC->GC_NewStringObject(stream.str()));

	errorObject->protectStatus = VMObject::NOT_PROTECTED;

	for (int i = callFrames.size() - 1; i >= 0; i--) {
		auto& fnFrame = callFrames[i];
		for (int j = fnFrame.scopeStack.size() - 1; j >= 0; j--) {

			//跳过当前作用域，因为逻辑上合理的异常处理程序不会出现在当前作用域
			//同时也避免上一次残留的其他异常处理程序被误触发
			//if (i == callFrames.size() - 1 && j == fnFrame.scopeStack.size() - 1) {
			//	continue;
			//}

			auto& scopeFrame = fnFrame.scopeStack[j];
			//向上找到异常处理程序
			if (scopeFrame.CheckControlFlowType(ScopeFrame::TRYCATCH) &&
				scopeFrame.exceptionHandlerEIP > scopeFrame.ep) { //判断一下当前的try块是否还有效
				fnFrame.virtualStack.resize(scopeFrame.spStart);
				fnFrame.localVariables.resize(scopeFrame.localvarStart);
				fnFrame.localVarNames.resize(scopeFrame.localvarStart);
				fnFrame.scopeStack.resize(j + 1);
				callFrames.resize(i + 1); //清理掉多余的栈帧及其存储的局部变量
				fnFrame = callFrames[i];
				scopeFrame = fnFrame.scopeStack[j]; //地址改变了，重新获取
				scopeFrame.ep = scopeFrame.exceptionHandlerEIP;
				VariableValue exref;
				exref.varType = ValueType::REF;
				exref.content.ref = errorObject;
				fnFrame.virtualStack.push_back(exref); //跳转到catch然后exobj压入栈

				//此时异常处理程序使命完成，重置当前作用域帧状态
				scopeFrame.ControlFlowFlag &= ~ScopeFrame::TRYCATCH; //清除异常处理程序标志
				scopeFrame.exceptionHandlerEIP = 0;
				return;
			}
		}
	}

	VMInstance->VM_UnhandledException(errorObject, this);
}

uint32_t getRawExecutionPosition(ScopeFrame* scope) {
	return scope->spStart + scope->ep;
}

//传入当前函数帧和尝试创建闭包的函数，不会对系统函数和已经有闭包的函数进行任何处理
static VariableValue __make_closure(VariableValue& function,FuncFrame* frame,VMWorker* worker) {
	ScriptFunction* sfn;
	if (function.varType != ValueType::FUNCTION) {
		return function;
	}
	else {
		sfn = function.content.function;
	}

	if (sfn->type != ScriptFunction::Local) {
		return function; //不对非字节码函数生成闭包
	}
	auto& outsideSym = sfn->funcImpl.local_func.outsideSymbols;

	uint16_t packageId = frame->functionInfo->packageId;

	auto& package = worker->VMInstance->loadedPackages[packageId];

	std::unordered_map<std::string, VariableValue> closureContainer;
	
	//查找哪些局部变量需要被捕获
	uint32_t index = 0;
	for (auto& varbNameId : frame->localVarNames) {
		//计算交集进行捕获
		for (auto symid : outsideSym) {
			if (symid == varbNameId) {
				std::string& varName = package.ConstStringPool[varbNameId]->implement.stringImpl;
				closureContainer[varName] = frame->localVariables[index];
			}
		}

		index++;
	}

	//查找父层闭包有没有需要被捕获的，进行闭包继承
	auto clos_find = frame->functionEnvSymbols.find("_clos");
	if (clos_find != frame->functionEnvSymbols.end()) {
		if ((*clos_find).second.getContentType() == ValueType::OBJECT) {
			for (auto& closVarb : (*clos_find).second.content.ref->implement.objectImpl) {
				for (auto symid : outsideSym) {
					if (package.ConstStringPool[symid]->implement.stringImpl == closVarb.first) {
						closureContainer[closVarb.first] = closVarb.second;
					}
				}
			}
		}
	}


	/*
	//零捕获优化：不创建闭包，直接返回原始函数
	if (closureContainer.size() == 0) {
		return function;
	}
	*/

	VMObject* closureFunction = worker->VMInstance->currentGC->Internal_NewObject(ValueType::FUNCTION);
	if (closureContainer.size() > 0) {
		VMObject* closureObject = worker->VMInstance->currentGC->Internal_NewObject(ValueType::OBJECT);
		//拷贝自己到闭包环境确保递归调用有效
		closureContainer[sfn->funcImpl.local_func.funcName] = CreateReferenceVariable(closureFunction); 
		closureObject->implement.objectImpl = closureContainer; //拷贝过去
		closureFunction->implement.closFuncImpl.closure = closureObject;
	}
	//propObject懒加载，必要的时候才分配节省内存
	closureFunction->implement.closFuncImpl.sfn = sfn;

	return CreateReferenceVariable(closureFunction);
}

static VariableValue* __vmworker_find_variable(VMWorker& worker, std::string& name) {
	auto& callingStack = worker.getCallingLink();
	//for (int i = callingStack.size() - 1; i >= 0; i--) {
		//FuncFrame& fnFrame = callingStack[i];
	FuncFrame& fnFrame = callingStack.back();
	auto envFind = fnFrame.functionEnvSymbols.find(name);
	if (envFind != fnFrame.functionEnvSymbols.end()) {
		return &(*envFind).second;
	}
	/*
	for (int j = fnFrame.scopeStack.size() - 1; j >= 0; j--) {
		ScopeFrame& scope = fnFrame.scopeStack[j];
		auto it = scope.scopeVariables.find(name);
		if (it != scope.scopeVariables.end()) {
			return &(*it).second;
		}
	}
	*/
	//}

	
	//从闭包查找（如有）
	auto clos_find = fnFrame.functionEnvSymbols.find("_clos"); //闭包对象通过参数隐式传递
	if (clos_find != fnFrame.functionEnvSymbols.end()) {
		auto& closureObject = (*clos_find).second;
		if (closureObject.getContentType() == ValueType::OBJECT) {
			auto& closureObjectMap = closureObject.content.ref->implement.objectImpl;
			auto closureInnerFind = closureObjectMap.find(name);
			if (closureInnerFind != closureObjectMap.end()) {
				return &(*closureInnerFind).second;
			}
		}
	}

	//从字节码程序集查找
	uint16_t packageId = fnFrame.functionInfo->packageId;
	VariableValue* find_bytecodeFunction = worker.VMInstance->GetBytecodeFunctionSymbol(packageId,name);
	if (find_bytecodeFunction) {
		return find_bytecodeFunction;
	}


	//如果都没有找到就尝试从全局变量表找

	static std::string g_name = "global";
	VariableValue* globalObject = worker.VMInstance->getGlobalSymbol(g_name);
	auto& globalObjectContainer = globalObject->content.ref->implement.objectImpl;
	auto globalObjectFind = globalObjectContainer.find(name);
	if (globalObjectFind != globalObjectContainer.end()) {
		return &(*globalObjectFind).second;
	}

	VariableValue* globalSymbolFind = worker.VMInstance->getGlobalSymbol(name);
	if (globalSymbolFind) { //全局符号表找不到就从系统单例符号表找
		return globalSymbolFind;
	}
	VariableValue* sysBuildinFind = SystemGetSymbol(name);
	return sysBuildinFind;
}


VariableValue VMWorker::VMWorkerTask() {

	GC* currentGC = VMInstance->currentGC;

	VariableValue lastestReturnValue;
	bool hasLastestReturnValue = false; //返回值当前是否可用，每次取出都需要将其设置为false

	

	while (true) {

		needResetLoop = false; //重置异常状态

		//检查是否需要GC，如果需要则进入安全点，阻塞到GC结束
		if (currentGC->GCRequired) {
			//printf("thread %d enterCheckPoint\n", currentWorkerId);
			currentGC->enterSTWSafePoint();
		}

		if (callFrames.size() == 0) {
			//此时Worker已经结束

			//如果没有返回值就压入默认的NULL作为返回值
			if (!hasLastestReturnValue) {
				new (&lastestReturnValue) VariableValue();
				lastestReturnValue.varType = ValueType::NULLREF;
			}
			return lastestReturnValue;
		}

		//改变CallingFrame之后请勿使用，需要使用请重新获取栈顶
		FuncFrame* currentFn = GetCurrentFrame();

		if (currentFn->scopeStack.empty()) {
			//作用域是空的，函数结束，压入默认返回值（NULLREF）

			//复制最终值到顶部，防止垂悬指针
			lastestReturnValue = VariableValue();
			hasLastestReturnValue = true;
			callFrames.pop_back(); //弹出栈帧

			//返回值向下传递，不对调用栈最底下的函数处理
			if (!callFrames.empty()) {
				callFrames.back().virtualStack.push_back(lastestReturnValue);
			}
			continue;
		}

		//改变ScopeStack之后请勿使用，需要使用请重新获取栈顶
		ScopeFrame* currentScope = &currentFn->scopeStack.back();

		//作用域结束了
		if (currentScope->ep >= currentScope->byteCodeLength) {
			currentFn->virtualStack.resize(currentScope->spStart);
			currentFn->localVariables.resize(currentScope->localvarStart);
			currentFn->localVarNames.resize(currentScope->localvarStart);
			currentFn->scopeStack.pop_back(); //作用域结束后弹出
			continue;
		}

		uint32_t rawep = currentScope->byteCodeStart + currentScope->ep;

		//函数结束了
		if (rawep >= currentFn->byteCodeLength) {
			//获取返回值
			lastestReturnValue = callFrames.back().returnValue;
			hasLastestReturnValue = true;
			callFrames.pop_back();
			continue;
		}

		OpCode::IOpCode op = (OpCode::IOpCode)currentFn->byteCode[rawep];

		uint16_t packageId = currentFn->functionInfo->packageId;

#if VM_DEBUGGER_ENABLED
		//启用调试器后每一步都调用检查点
		Debugger_CheckPoint(this, rawep, packageId);

#endif

#ifdef _DEBUG

		/*

		static std::unordered_map<int, int> stat;
		if (op != OpCode::RET) {
			stat[op]++;
		}
		else {
			for (auto& entry : stat) {
				printf("[%s]%d\n", IOpCodeStr[entry.first], entry.second);
			}
		}

		//======调试用=======
			system("cls"); //clear();
			printf("Command: %s\r\n", IOpCodeStr[op]);
			printf("EIP: %d\r\n", rawep);
			printf("===Environment===\r\n");
			for (auto& env_pair : currentFn->functionEnvSymbols) {
				printf("[%s]%s\n", wstring_to_string(env_pair.first).c_str(), wstring_to_string(env_pair.second.ToString()).c_str());
			}
			printf("===Variables===\r\n");
			for (int i = 0; i < currentFn->localVariables.size(); i++) {

				uint16_t packageId = currentFn->functionInfo->packageId;
				auto& package = VMInstance->loadedPackages[packageId];
				auto& name = package.ConstStringPool[currentFn->localVarNames[i]]->implement.stringImpl;
				auto& varValue = currentFn->localVariables[i];

				printf("[%s]%s\n", wstring_to_string(name).c_str(), wstring_to_string(varValue.ToString()).c_str());
			}
			printf("===Stack===\r\n");
			int stki = 0;
			for (auto& varb : currentFn->virtualStack) {
				stki++;
				printf("[%d]%ws\r\n", stki, varb.ToString().c_str());
			}

			printf("\r\n\r\n===Scope===\r\n");

			for (auto& scope : currentFn->scopeStack) {
				printf("{start:%d ,length:%d ,ep:%d attr:%d }\r\n", scope.byteCodeStart, scope.byteCodeLength, scope.ep,scope.ControlFlowFlag);
			}

			std::cin.get();

		//======调试用=======

		*/

#endif

		switch (op)
		{
		case OpCode::ADD:
		{
			/*
			* PUSH left
			* PUSH right
			* ADD
			*/
			VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable();
			VariableValue res;
			//判断如果有一边不是数字并且有一边是字符串就执行字符串拼接
			//非引用类型直接访问varType即可，其他操作符同理
			if (left->varType != ValueType::NUM || right->varType != ValueType::NUM) {

				//如果有一边是字符串就是合法的
				if (left->getContentType() == ValueType::STRING || right->getContentType() == ValueType::STRING) {
					std::string resstr = left->ToString() + right->ToString();
					res.varType = ValueType::REF;
					res.content.ref = VMInstance->currentGC->Internal_NewObject(ValueType::STRING);
					res.content.ref->implement.stringImpl = resstr;
				}
				else {
					ThrowError("ADD operation not vaild");
				}
			}
			else {
				//数字相加
				res.varType = ValueType::NUM;
				res.content.number = left->content.number + right->content.number;
			}
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.pop_back();

			currentFn->virtualStack.push_back(res);

			break;
		}

		case OpCode::SUB:
			BINARY_NUMERIC_OP(BINARY_OP_LEFT_NUM - BINARY_OP_RIGHT_NUM, "SUB指令只能接受NUM类型");
		case OpCode::MUL:
			BINARY_NUMERIC_OP(BINARY_OP_LEFT_NUM * BINARY_OP_RIGHT_NUM, "MUL指令只能接受NUM类型");
		case OpCode::DIV:
			BINARY_NUMERIC_OP(BINARY_OP_LEFT_NUM / BINARY_OP_RIGHT_NUM, "DIV指令只能接受NUM类型");
		case OpCode::MOD:
			BINARY_NUMERIC_OP(fmod(BINARY_OP_LEFT_NUM, BINARY_OP_RIGHT_NUM), "MOD指令只能接受NUM类型");
		case OpCode::NOT:
		{
			VariableValue* target = currentFn->virtualStack.back().getRawVariable();
			VariableValue res;
			res.varType = ValueType::BOOL;
			res.content.boolean = !target->Truty();
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.push_back(res);
			break;
		}
		case OpCode::NEG:
		{
			VariableValue* target = currentFn->virtualStack.back().getRawVariable();
			VariableValue res;
			res.varType = ValueType::NUM;
			res.content.number = -target->content.number;
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.push_back(res);
			break;
		}
		case OpCode::BIT_AND:
			BINARY_NUMERIC_OP((double)((uint32_t)BINARY_OP_LEFT_NUM & (uint32_t)BINARY_OP_RIGHT_NUM), "BIT_AND指令只能接受NUM类型");
		case OpCode::BIT_OR:
			BINARY_NUMERIC_OP((double)((uint32_t)BINARY_OP_LEFT_NUM | (uint32_t)BINARY_OP_RIGHT_NUM), "BIT_OR指令只能接受NUM类型");
		case OpCode::BIT_XOR:
			BINARY_NUMERIC_OP((double)((uint32_t)BINARY_OP_LEFT_NUM ^ (uint32_t)BINARY_OP_RIGHT_NUM), "BIT_XOR指令只能接受NUM类型");
		case OpCode::BIT_NOT:
		{
			VariableValue* target = currentFn->virtualStack.back().getRawVariable();
			VariableValue res;
			res.varType = ValueType::NUM;
			res.content.number = (double)(~(uint64_t)target->content.number);
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.push_back(res);
			break;
		}
		case OpCode::SHL:
			BINARY_NUMERIC_OP((double)((uint64_t)BINARY_OP_LEFT_NUM << (uint64_t)BINARY_OP_RIGHT_NUM), "SHL指令只能接受NUM");
		case OpCode::SHR:
			BINARY_NUMERIC_OP((double)((uint64_t)BINARY_OP_LEFT_NUM >> (uint64_t)BINARY_OP_RIGHT_NUM), "SHL指令只能接受NUM");
		case OpCode::EQUAL:
		{
			VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable();
			VariableValue res;
			res.varType = ValueType::BOOL;
			res.content.boolean = (*left) == (*right);
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.push_back(res);
			break;
		}
		case OpCode::NOT_EQUAL:
		{
			VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable();
			VariableValue res;
			res.varType = ValueType::BOOL;
			res.content.boolean = (*left) != (*right);
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.pop_back();
			currentFn->virtualStack.push_back(res);
			break;
		}
		case OpCode::LOWER_EQUAL:
			BINARY_NUMERIC_OP_RETBOOL(BINARY_OP_LEFT_NUM <= BINARY_OP_RIGHT_NUM, "比较运算符只接受NUM");
		case OpCode::GREATER_EQUAL:
			BINARY_NUMERIC_OP_RETBOOL(BINARY_OP_LEFT_NUM >= BINARY_OP_RIGHT_NUM, "比较运算符只接受NUM");
		case OpCode::LOWER:
			BINARY_NUMERIC_OP_RETBOOL(BINARY_OP_LEFT_NUM < BINARY_OP_RIGHT_NUM, "比较运算符只接受NUM");
		case OpCode::GREATER:
			BINARY_NUMERIC_OP_RETBOOL(BINARY_OP_LEFT_NUM > BINARY_OP_RIGHT_NUM, "比较运算符只接受NUM");
		case OpCode::AND:
			BINARY_BOOLEAN_OP(BINARY_OP_LEFT_BOOL && BINARY_OP_RIGHT_BOOL, "比较运算符只接受NUM");
		case OpCode::OR:
			BINARY_BOOLEAN_OP(BINARY_OP_LEFT_BOOL || BINARY_OP_RIGHT_BOOL, "比较运算符只接受NUM");
		case OpCode::SCOPE_PUSH:
		{
			ScopeFrame scope;
			//跳过这条指令就是作用域帧起点
			scope.byteCodeStart = rawep + OpCode::instructionSize[op];
			//scope.byteCodeLength = *(uint32_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&scope.byteCodeLength, currentFn->byteCode + rawep + 1, sizeof(int));
			//读取1字节flag
			//scope.controlHandlerType = (ScopeFrame::ControlFlowType)*(currentFn->byteCode + rawep + sizeof(uint32_t) + 1);
			scope.ControlFlowFlag |= *(currentFn->byteCode + rawep + sizeof(uint32_t) + 1);
			scope.spStart = currentFn->virtualStack.size();
			scope.localvarStart = currentFn->localVariables.size();
			scope.ep = 0;
			currentScope->ep += scope.byteCodeLength + OpCode::instructionSize[op]; //栈帧执行指针跳过，这样新的作用域销毁后执行下面的
			currentFn->scopeStack.push_back(scope);

			continue; //currentScope已被vector重新分配，已经手动不进指令，重启循环进入新scope
		}
		case OpCode::BREAK:
		{
			bool success = false;
			for (int i = currentFn->scopeStack.size() - 1; i >= 0; i--) {
				if (currentFn->scopeStack[i].CheckControlFlowType(ScopeFrame::LOOP)) {
					currentFn->virtualStack.resize(currentFn->scopeStack[i].spStart);
					currentFn->localVariables.resize(currentFn->scopeStack[i].localvarStart);
					currentFn->localVarNames.resize(currentFn->scopeStack[i].localvarStart);
					currentFn->scopeStack.resize(i); //弹出包括这个LOOP本身的作用域
					success = true;
					break;
				}
			}
			if (!success) {
				ThrowError("无法执行break语句，无目标作用域可跳转");
			}
			continue; //改变了作用域帧就不需要让下面不进了，因为currentScope失效（下面那个同理）
		}
		case OpCode::CONTINUE:
		{
			bool success = false;
			for (int i = currentFn->scopeStack.size() - 1; i >= 0; i--) {
				if (currentFn->scopeStack[i].CheckControlFlowType(ScopeFrame::LOOP)) {
					currentFn->virtualStack.resize(currentFn->scopeStack[i].spStart);
					currentFn->localVariables.resize(currentFn->scopeStack[i].localvarStart);
					currentFn->localVarNames.resize(currentFn->scopeStack[i].localvarStart);
					currentFn->scopeStack.resize(i + 1); //弹出LOOP之上的所有作用域帧
					currentFn->scopeStack.back().ep = 0; //重新开始执行这个作用域
					success = true;
					break;
				}
			}
			if (!success) {
				ThrowError("无法执行break语句，无目标作用域可跳转");
			}
			continue;
		}
		case OpCode::POP:
		{
			currentFn->virtualStack.pop_back();
			break;
		}
		case OpCode::PUSH_NUM:
		{
			double num;
			//= *(double*)(currentFn->byteCode + rawep + 1);
			memcpy(&num, currentFn->byteCode + rawep + 1, sizeof(double));
			VariableValue v;
			v.varType = ValueType::NUM;
			v.content.number = num;
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::PUSH_PTR:
		{
			uint64_t ptrv;
			//= *(uint64_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&ptrv, currentFn->byteCode + rawep + 1, sizeof(uint64_t));
			VariableValue v;
			v.varType = ValueType::PTR;
			v.content.ptr = ptrv;
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::PUSH_STR:
		{
			uint16_t strid;
			//= *(uint16_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&strid, currentFn->byteCode + rawep + 1, sizeof(uint16_t));

			VariableValue v;
			auto& strPool = VMInstance->loadedPackages[packageId].ConstStringPool;
			VMObject* str = strPool[strid];
			v.varType = ValueType::REF;
			v.content.ref = str;
			currentFn->virtualStack.push_back(v);
			break;

		}
		case OpCode::PUSH_BOOL:
		{
			bool opbool = *(currentFn->byteCode + rawep + 1) == 1;
			VariableValue v;
			v.varType = ValueType::BOOL;
			v.content.boolean = opbool;
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::PUSH_NULL:
		{
			currentFn->virtualStack.push_back(VariableValue());
			break;
		}
		case OpCode::DUP_PUSH:
		{
			VariableValue v = currentFn->virtualStack.back();
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::JMP:
		{
			uint32_t address;
			//= *(uint32_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&address, currentFn->byteCode + rawep + 1, sizeof(uint32_t));
			currentScope->ep += address;

			break;
		}
		case OpCode::JMP_IF_FALSE:
		{
			VariableValue* codition = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			uint32_t addr;
			//= *(uint32_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&addr, currentFn->byteCode + rawep + 1, sizeof(uint32_t));
			if (!codition->Truty()) {
				currentScope->ep += addr;

			}

			currentFn->virtualStack.pop_back();
			break;
		}
		case OpCode::CALLFUNC:
		{
			//[栈顶][参数1..N][函数对象][...]
			uint8_t op_argCount = *(currentFn->byteCode + rawep + 1);
			VariableValue* funcRefVal = currentFn->virtualStack[currentFn->virtualStack.size() - 1 - op_argCount].getRawVariable();
			if (funcRefVal->getContentType() != ValueType::FUNCTION) {
				ThrowError("not a function");
				continue;
			}

			ScriptFunction* funcInfo;
			VMObject* closureObject;
			if (funcRefVal->varType == ValueType::REF) {
				funcInfo = funcRefVal->content.ref->implement.closFuncImpl.sfn;
				closureObject = funcRefVal->content.ref->implement.closFuncImpl.closure;
			}
			else {
				funcInfo = funcRefVal->content.function;
				closureObject = NULL;
			}
			uint8_t argCount = funcInfo->argumentCount;

			//不是可变参数方法检查参数数量
			if (argCount != DYNAMIC_ARGUMENT && op_argCount != argCount) {
				ThrowError("function argument count not equal.");
				continue;
			}

			std::vector<VariableValue> arguments;

			//从栈顶顶开始复制参数
			for (int i = 0; i < op_argCount; i++) {
				//对压入参数列表的VariableValue归一化值复制，避免弹出导致引用失效
				VariableValue arg = *currentFn->virtualStack[currentFn->virtualStack.size() - 1 - i].getRawVariable();
				if (arg.varType == ValueType::FUNCTION) {
					//对可能逃逸的值函数进行闭包捕获
					arg = __make_closure(arg, currentFn, this);
				}
				
				arguments.push_back(arg);
			}


			//弹出函数对象和参数
			currentFn->virtualStack.resize(currentFn->virtualStack.size() - op_argCount - 1);

			if (funcInfo->type == ScriptFunction::Local) {
				currentScope->ep += OpCode::instructionSize[op]; //接下来要改变栈帧了，代替循环尾部进行不进
				funcInfo->InvokeFunc(arguments, funcRefVal->thisValue,closureObject, this);
				
				continue;
			}
			else if (funcInfo->type == ScriptFunction::System) {
				//currentGC->IgnoreWorkerCount_Inc(); //GC标记原生边界
				auto funcResult = funcInfo->InvokeFunc(arguments, funcRefVal->thisValue,NULL, this);
				//currentGC->IgnoreWorkerCount_Dec();
				//如果原生函数未发生异常就压入返回值
				
				if (!needResetLoop) {
					if (funcResult.varType == ValueType::REF) {
						funcResult.content.ref->protectStatus = VMObject::NOT_PROTECTED;
						//currentGC->SetObjectProtect(funcResult.content.ref,false);
					}
					currentFn->virtualStack.push_back(funcResult);
				}
			}



			break;
		}
		case OpCode::RET:
		{
			VariableValue* retValue = currentFn->virtualStack.back().getRawVariable();

			//如果是函数就处理闭包
			VariableValue changeBuffer; //如果需要改变地址，用其作为缓存区
			if (retValue->getContentType() == ValueType::FUNCTION) {
				changeBuffer = __make_closure(*retValue, currentFn, this);
				retValue = &changeBuffer;
			}

			//复制最终值到顶部，防止垂悬指针
			lastestReturnValue = *retValue;
			hasLastestReturnValue = true;
			currentFn->virtualStack.pop_back();
			callFrames.pop_back(); //弹出栈帧

			//返回值向下传递，不对调用栈最底下的函数处理
			if (!callFrames.empty()) {
				callFrames.back().virtualStack.push_back(lastestReturnValue);
			}
			//栈帧已经改变，直接重置VM循环
			continue;
		}
		case OpCode::TRY_ENTER:
		{
			//一个包含try块+try块末尾JMP指令的大小
			uint32_t tryBlockJmpSize;
			//= *(uint32_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&tryBlockJmpSize, currentFn->byteCode + rawep + 1, sizeof(uint32_t));
			//计算catch块地址（ep + 当前指令大小 + try块跳过大小） 补偿当前指令大小
			currentScope->exceptionHandlerEIP = currentScope->ep + tryBlockJmpSize + OpCode::instructionSize[op];
			currentScope->ControlFlowFlag |= ScopeFrame::TRYCATCH;

			break;
		}
		case OpCode::TRY_END:
		{
			break;
		}
		case OpCode::THROW:
		{
			auto varb = currentFn->virtualStack.back().getRawVariable();

			if (varb->getContentType() == ValueType::STRING) {

				ThrowError(*varb->getRawVariable());
			}
			else {
				VariableValue exmsg;
				exmsg.varType = ValueType::REF;
				exmsg.content.ref = VMInstance->currentGC->Internal_NewObject(ValueType::STRING);
				exmsg.content.ref->implement.stringImpl = "exception has thrown";
				ThrowError(exmsg);
			}



			continue; //抛出异常后重置VM循环状态
		}
		case OpCode::NEW_OBJ:
		{
			VMObject* new_object = VMInstance->currentGC->Internal_NewObject(ValueType::OBJECT);

			VariableValue v;
			v.varType = ValueType::REF;
			v.content.ref = new_object;
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::NEW_ARR:
		{
			VMObject* new_array = VMInstance->currentGC->Internal_NewObject(ValueType::ARRAY);
			//new (&new_array->implement.arrayImpl) std::vector<VariableValue>();
			VariableValue v;
			v.varType = ValueType::REF;
			v.content.ref = new_array;
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::STORE:
		{
			VariableValue* right = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			VariableValue* left = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable();
			if (left->readOnly) {
				ThrowError("try assign readonly value");
				continue;
			}
			//如果是函数就处理闭包
			VariableValue changeBuffer; //如果需要改变地址，用其作为缓存区
			if (right->getContentType() == ValueType::FUNCTION) {
				changeBuffer = __make_closure(*right, currentFn, this);
				right = &changeBuffer;
			}

			left->varType = right->varType;
			left->content = right->content;
			currentFn->virtualStack.pop_back(); //清理掉右操作数
			break;
		}
		case OpCode::STORE_LOCAL: //自带字符串索引，存入局部变量，如果不存在自动创建
		{
			uint16_t nameStringIndex;
			//= *(uint16_t*)(currentFn->byteCode + rawep + 1);
			memcpy(&nameStringIndex, currentFn->byteCode + rawep + 1, sizeof(uint16_t));
			VariableValue* target = currentFn->virtualStack.back().getRawVariable();

			//如果是函数就处理闭包
			VariableValue changeBuffer; //如果需要改变地址，用其作为缓存区
			if (target->getContentType() == ValueType::FUNCTION) {
				changeBuffer = __make_closure(*target, currentFn, this);
				target = &changeBuffer;
			}


			//判断是否已经定义过
			//通过寻找此str_id是否存在，且str_id对应的索引是否有效
			/*
			if (currentFn->localVarNames) {
				uint16_t id = currentFn->functionInfo->packageId;
				auto& strPool = VMInstance->loadedPackages[id].ConstStringPool;
				std::string& nameString = strPool[nameStringIndex]->implement.stringImpl;
				ThrowError(nameString + " has already been declared");
				continue;
			}
			*/

			currentFn->localVariables.push_back(*target);
			currentFn->localVarNames.push_back(nameStringIndex);
			break;
		}
		case OpCode::DEF_LOCAL:
		{
			uint16_t str_id = 0;
			memcpy(&str_id, currentFn->byteCode + rawep + 1, sizeof(uint16_t));
			VariableValue v;
			v.varType = ValueType::NULLREF;

			std::string str = VMInstance->loadedPackages[packageId].ConstStringPool[str_id]->implement.stringImpl;

			//currentScope->scopeVariables[str] = v;

			currentFn->localVariables.push_back(v);
			currentFn->localVarNames.push_back(str_id);

			//currentFn->virtualStack.pop_back();
			break;
		}
		case OpCode::LOAD_LOCAL:
		{
			uint16_t var_id = 0;
			memcpy(&var_id, currentFn->byteCode + rawep + 1, sizeof(uint16_t));
			VariableValue v;
			v.varType = ValueType::BRIDGE;
			v.content.bridge_ref = &currentFn->localVariables[var_id];
			currentFn->virtualStack.push_back(v);
			break;
		}
		case OpCode::DEL_DEF:
			break;
		case OpCode::LOAD_VAR:
		{
			VariableValue* varName = currentFn->virtualStack.back().getRawVariable();
			std::string str = varName->content.ref->implement.stringImpl;

			VariableValue* find = __vmworker_find_variable(*this, str);
			if (!find) {
				ThrowError("cannot find symbol:" + str);
				continue;
			}
			VariableValue v;
			v.varType = ValueType::BRIDGE;
			v.content.bridge_ref = find;
			currentFn->virtualStack.pop_back(); //删掉变量名操作数
			currentFn->virtualStack.push_back(v);

			break;
		}
		case OpCode::GET_FIELD:
		{
			VariableValue* fieldName = currentFn->virtualStack[currentFn->virtualStack.size() - 1].getRawVariable();
			VariableValue* parent = currentFn->virtualStack[currentFn->virtualStack.size() - 2].getRawVariable();
			VariableValue result;
			ValueType::IValueType parentType = parent->getContentType();
			restart_get_field: //类型切换复用标签
			if (parentType == ValueType::ARRAY) {
				//Array类型返回的不是BRIDGE类型，比如内置方法/length等属性
				//数组元素通过Array.get方法返回的BRIDGE类型VariableValue实现修改和访问
				result = GetArraySymbol(fieldName->content.ref->implement.stringImpl, parent->content.ref);
			}
			else if (parentType == ValueType::STRING) {
				result = GetStringValSymbol(fieldName->content.ref->implement.stringImpl, parent->content.ref);
			}
			else if (parentType == ValueType::FUNCTION && parent->varType == ValueType::REF) {
				//函数对象允许存储属性

				//需要时分配
				if (!parent->content.ref->implement.closFuncImpl.propObject) {
					parent->content.ref->implement.closFuncImpl.propObject = currentGC->Internal_NewObject(ValueType::OBJECT);
				}

				VMObject* propObject = parent->content.ref->implement.closFuncImpl.propObject;

				auto& obj_container = propObject->implement.objectImpl;
				auto key = fieldName->ToString();
				result.varType = ValueType::BRIDGE;

				//从符号表取出的拷贝引用设置thisValue绑定
				result.content.bridge_ref = &obj_container[key];
				result.content.bridge_ref->thisValue = parent->content.ref;

			}
			else if (parentType == ValueType::OBJECT) {
				//返回BRIDGE类型，确保修改对象可以修改obj.member

				auto& obj_container = parent->content.ref->implement.objectImpl;
				auto key = fieldName->ToString();
				result.varType = ValueType::BRIDGE;

				VariableValue buildinMember = GetObjectBuildinSymbol(key, parent->content.ref);

				//map隐式创建可key，这个Bridge指针生命周期只在这条表达式内
				//优先查找对象符号表，找不到就看看内置符号表有没有，如果没有就创建有就返回
				if (obj_container.find(key) != obj_container.end()) {
					//从符号表取出的拷贝引用设置thisValue绑定
					result.content.bridge_ref = &obj_container[key];
					result.content.bridge_ref->thisValue = parent->content.ref;
				}
				else {
					if (buildinMember.varType != ValueType::NULLREF) {
						result = buildinMember;
					}
					else {
						result.content.bridge_ref = &obj_container[key];
						result.content.bridge_ref->thisValue = parent->content.ref;
					}
				}

			}

			if (result.varType == ValueType::NULLREF) {
				ThrowError("can not find symbol:" + fieldName->ToString());
				continue;
			}

			currentFn->virtualStack.resize(currentFn->virtualStack.size() - 2);
			currentFn->virtualStack.push_back(result);

			break;
		}
		case OpCode::CONST_STR:
		{
			/*
			uint32_t length = *(uint32_t*)(currentFn->byteCode + rawep + 1);
			char* str_start = (char*)(currentFn->byteCode + rawep + sizeof(uint32_t) + 1);
			std::string str(str_start, length);
			VMObject vm_str(ValueType::STRING); //创建不归GC管理的常量字符串对象
			vm_str.implement.stringImpl = str;
			currentFn->constStringPool.push_back(vm_str);
			currentScope->ep += 1 + 4 + length * sizeof(char); //头+4字节长度+内容
			*/
			//废弃的指令，抛出错误
			ThrowError("CONST_STR code not supported");
			break;
		}
		default:
			break;
		}

		if (!needResetLoop) //如果发生需要重置的操作（比如异常抛出）则不进行不进
			currentScope->ep += OpCode::instructionSize[op];
	}

	/*
	//虚拟机字节码循环结束后，从VM.workers移除自身
	for (auto it = VMInstance->workers.begin(); it != VMInstance->workers.end();it++) {
		if ((*it).get() == this) {
			VMInstance->workers.erase(it);
			break;
		}
	}
	*/
}




#pragma endregion


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

	worker->getCallingLink().clear(); //终止worker
}


