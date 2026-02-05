#include "VMDebugger.h"

#if VM_DEBUGGER_ENABLED

#include <unordered_map>
#include <vector>
#include <sstream>
#include <unordered_set>

#include "VM.h"
#include "GC.h"

#include "StringConverter.h"

void* DebugGEL; //全局调试器锁，强制串行化解释器检查点

class BreakPointKey {
public:
	uint16_t packageId = 0;
	uint16_t functionNameId = 0;
	uint32_t eip = 0;
};

class BreakPointInfo {
public:
	BreakPointKey key;
	VMWorker* worker = NULL;
};

std::unordered_map<uint32_t, std::vector<BreakPointKey>> BreakPoints;

static std::vector<std::string> _dbg_split(const std::string& str) {
	std::vector<std::string> tokens;
	std::istringstream iss(str);
	std::string token;

	while (iss >> token) {
		tokens.push_back(token);
	}
	return tokens;
}

static bool _get_id_by_name(uint16_t* out_id, std::string& name, VM* vm, uint16_t packageId) {
	auto& vec = vm->loadedPackages[packageId].ConstStringPool;
	uint32_t id = 0;
	for (auto& str : vec) {
		if (str->type == ValueType::STRING && str->implement.stringImpl == name) {
			*out_id = id;
			return true;
		}
		id++;
	}
	return false;
}



const char* IOpCodeStr_DBG[] = {
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
	"JMP_IF_TRUE",
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

void DumpFrame(VMWorker* worker) {
	auto& callFrames = worker->getCurrentCallingLink();
	auto& currentFn = callFrames.back();
	auto& currentScope = currentFn.scopeStack.back();

	std::ostringstream oss;

	// cmd
	oss.str("");
	oss << "cmd:" << IOpCodeStr_DBG[currentFn.byteCode[currentScope.byteCodeStart + currentScope.ep]];
	debuggerImpl.SendToDebugger(oss.str().c_str());

	// eip
	oss.str("");
	oss << "eip:" << (currentScope.byteCodeStart + currentScope.ep);
	debuggerImpl.SendToDebugger(oss.str().c_str());

	// env
	for (auto& env_pair : currentFn.functionEnvSymbols) {
		oss.str("");
		oss << "var:[" << env_pair.first << "]"
			<< env_pair.second.ToString();
		debuggerImpl.SendToDebugger(oss.str().c_str());
	}

	// var
	for (int i = 0; i < currentFn.localVariables.size(); i++) {
		uint16_t packageId = currentFn.functionInfo->packageId;
		auto& package = worker->VMInstance->loadedPackages[packageId];
		auto& name = package.ConstStringPool[currentFn.localVarNames[i]]->implement.stringImpl;
		auto& varValue = currentFn.localVariables[i];

		oss.str("");
		oss << "var:[" << name << "]"
			<< varValue.ToString();
		debuggerImpl.SendToDebugger(oss.str().c_str());
	}

	// stk
	int stki = 0;
	for (auto& varb : currentFn.virtualStack) {
		stki++;
		oss.str("");
		oss << "stk:[" << stki << "]" << varb.ToString();
		debuggerImpl.SendToDebugger(oss.str().c_str());
	}

	// scp
	for (auto& scope : currentFn.scopeStack) {
		oss.str("");
		oss << "scp:{start:" << scope.byteCodeStart
			<< ", length:" << scope.byteCodeLength
			<< ", ep:" << scope.ep
			<< ", attr:" << std::to_string(scope.ControlFlowFlag) << "}";
		debuggerImpl.SendToDebugger(oss.str().c_str());
	}
}

void DumpStack(VMWorker* worker) {
	std::ostringstream stream;
	auto& callFrames = worker->getCurrentCallingLink();
	for (int i = callFrames.size() - 1; i >= 0; i--) {
		auto& fnFrame = callFrames[i];
		stream << "callstk:";
		stream << "at " << fnFrame.functionInfo->funcName << "(";
		for (int argIndex = 0; argIndex < fnFrame.functionInfo->arguments.size(); argIndex++) {
			if (argIndex != 0) stream << ",";
			auto argName = worker->VMInstance->loadedPackages[fnFrame.functionInfo->packageId].ConstStringPool[fnFrame.functionInfo->arguments[argIndex]]->ToString();
			stream << argName;
			stream << "=";
			stream << fnFrame.localVariables[argIndex].ToString();
		}
		stream << ") offset:" << fnFrame.scopeStack.back().byteCodeStart + fnFrame.scopeStack.back().ep;
		debuggerImpl.SendToDebugger(stream.str().c_str());
		stream.str("");
	}

}

void DumpGlobal(VMWorker* worker) {
	VM* vm = worker->VMInstance;
	std::stringstream stream;
	for (auto& pair : vm->globalSymbols) {
		stream << "var:";
		stream << "[" << pair.first << "]";
		stream << pair.second.ToString() << "\n";
		debuggerImpl.SendToDebugger(stream.str().c_str());
		stream.str("");
	}

}

bool NeedBreakPoint(uint16_t nameId, uint32_t eip, uint16_t packageId) {
	auto it = BreakPoints.find(eip);
	if (it == BreakPoints.end()) {
		return false;
	}
	auto& vec = (*it).second;

	for (auto& key : vec) {
		if (key.functionNameId == nameId && key.packageId == packageId) {
			return true;
		}
	}
	return false;
}

bool RemoveBreakPoint(uint16_t nameId, uint32_t eip, uint16_t packageId) {

	auto it = BreakPoints.find(eip);
	if (it == BreakPoints.end()) {
		return false;
	}
	auto& vec = it->second;

	for (auto it = vec.begin(); it != vec.end(); it++) {
		auto& key = *it;
		if (key.functionNameId == nameId && key.packageId == packageId) {
			vec.erase(it);
			if (vec.size() == 0) {
				BreakPoints.erase(eip);
			}
			return true;
		}
	}
	return false;
}

bool DebuggerInited = false;
enum BreakPointState {
	NONE, //无
	BREAKED, //已暂停
	RESUMING, //结束，正在恢复
} currentBreakState;

bool BreakWithManualSTW = false; //是否是用户手动使用stw命令暂停

BreakPointInfo cur_breakPoint;

std::unordered_set<VMWorker*> need_step_over;;

void* DebuggerProc(void* param) {
	VM* vm = (VM*)param;
	while (true) {
		if (debuggerImpl.IsDebuggerConnected()) {

			debuggerImpl.SendToDebugger("done"); //消息终止(放在前面，continue后还可以生效)

			std::string command = debuggerImpl.ReadFromDebugger();
			auto split = _dbg_split(command);
			if (split.size() == 0) {
				continue;
			}
			if (split[0] == "brk") { //增加断点 brk <packageName> <funcName> <eip>
				char* endPtr = 0;
				uint32_t packageId = 0;
				for (auto& package : vm->loadedPackages) {
					if (package.second.packageName == split[1]) {
						packageId = package.first;
						break;
					}
				}
				if (packageId == 0) {
					debuggerImpl.SendToDebugger("err:can't find package");
					continue;
				}
				uint32_t eip = strtol(split[3].c_str(), &endPtr, 10);
				uint16_t nameId = 0;
				if (!_get_id_by_name(&nameId, split[2], vm, packageId)) {
					debuggerImpl.SendToDebugger("err:can't find func");
					continue;
				}
				BreakPointKey key;
				key.eip = eip;
				key.functionNameId = nameId;
				key.packageId = packageId;
				BreakPoints[eip].push_back(key);
			}
			else if (split[0] == "brk_rm") {
				char* endPtr = 0;
				uint32_t packageId = 0;
				for (auto& package : vm->loadedPackages) {
					if (package.second.packageName == split[1]) {
						packageId = package.first;
						break;
					}
				}
				if (packageId == 0) {
					debuggerImpl.SendToDebugger("err:can't find package");
					continue;
				}
				uint32_t eip = strtol(split[3].c_str(), &endPtr, 10);
				uint16_t nameId = 0;
				if (!_get_id_by_name(&nameId, split[2], vm, packageId)) {
					debuggerImpl.SendToDebugger("err:can't find func");
					continue;
				}
				RemoveBreakPoint(nameId, eip, packageId);
			}
			else if (split[0] == "brk_ls") {
				std::ostringstream stream;
				for (auto& pair : BreakPoints) {
					for (auto& brk : pair.second) {
						stream << "brkp:";
						std::string packageName;
						PackageContext* package = NULL;
						for (auto& pck : vm->loadedPackages) {
							if (brk.packageId == pck.first) {
								package = &pck.second;
								packageName = pck.second.packageName;
								break;
							}
						}
						if (!package) {
							continue;
						}
						std::string fnName = package->ConstStringPool[brk.functionNameId]->implement.stringImpl;
						stream << packageName;
						stream << " ";
						stream << fnName;
						stream << " ";
						stream << std::to_string(brk.eip);
						debuggerImpl.SendToDebugger(stream.str().c_str());
						stream.str("");
					}
				}
			}
			else if (split[0] == "workers") { //查看workers
				for (auto& worker : vm->workers) {
					debuggerImpl.SendToDebugger(("worker:" + std::to_string(worker->currentWorkerId)).c_str());
				}
			}
			else if (split[0] == "stw") { //暂停整个VM并显示给定worker stw <worker_id?可选>
				if (currentBreakState == BREAKED) {
					debuggerImpl.SendToDebugger("err:current is paused");
				}
				else {

					BreakWithManualSTW = true;

					vm->currentGC->StopTheWorld();

					currentBreakState = BREAKED;

					cur_breakPoint.worker = vm->workers[0]; //默认第一个worker

					debuggerImpl.SendToDebugger("brk_trig"); //手动发送断点响应信号
				}

				/*
				char* endPtr = 0;
				//默认获取主线程id
				uint32_t worker_id = vm->workers[0].get()->currentWorkerId;
				if (split.size() > 1) {
					worker_id = strtol(split[1].c_str(), &endPtr, 10);
				}
				bool success = false;
				for (auto& worker : vm->workers) {
					if (worker.get()->currentWorkerId == worker_id) {
						need_step_over.insert(worker.get());
						success = true;
					}
				}

				debuggerImpl.SendToDebugger(success ? "msg:success" : "err:failed");
				*/
			}
			else if (currentBreakState == BREAKED) {
				if (split[0] == "resume") {
					currentBreakState = RESUMING;
					//如果是手动stw暂停的需要手动恢复世界运行
					if (BreakWithManualSTW) {
						vm->currentGC->ResumeTheWorld();
						currentBreakState = NONE;
					}
				}
				else if (split[0] == "dump") {
					DumpFrame(cur_breakPoint.worker);
				}
				else if (split[0] == "global") {
					DumpGlobal(cur_breakPoint.worker);
				}
				else if (split[0] == "stack") {
					DumpStack(cur_breakPoint.worker);
				}
				else if (split[0] == "step") {
					need_step_over.insert(cur_breakPoint.worker);
					currentBreakState = RESUMING;
				}
				else if (split[0] == "set_wrk") { //set_wrk <worker_id>
					char* endPtr;
					uint32_t worker_id = strtol(split[1].c_str(), &endPtr, 10);

					bool found = false;
					for(auto& worker : vm->workers){
						if(worker->currentWorkerId == worker_id){
							cur_breakPoint.worker = worker;
							found = true;
							break;
						}
					}

					if(!found){
						debuggerImpl.SendToDebugger("err:invaild worker_id");
					}

					/*
					if (vm->tasks.find(worker_id) == vm->tasks.end()) {
						debuggerImpl.SendToDebugger("err:invaild worker_id");
					}
					else {
						cur_breakPoint.worker = vm->tasks[worker_id].worker;
					}
					*/
				}
			}



		}
	}
}

void Debugger_CheckPoint(VMWorker* worker, uint32_t eip, uint16_t packageId)
{
	if (!debuggerImpl.implemented) {
		return; //检查调试器通信层是否实现
	}
	if (!DebuggerInited) {
		DebuggerInited = true;
		DebugGEL = platform.MutexCreate();
		uint32_t tid = platform.StartThread(DebuggerProc, worker->VMInstance);
		if (!tid) {
			printf("debugger load failed\n");
		}
		uint32_t packageId = 1;
		uint32_t eip = 6;
		uint16_t nameId = 0;
		std::string name = "main_entry";
		if (!_get_id_by_name(&nameId, name, worker->VMInstance, packageId)) {
			debuggerImpl.SendToDebugger("err:can't find symbol");
		}
		BreakPointKey key;
		key.eip = eip;
		key.functionNameId = nameId;
		key.packageId = packageId;
		BreakPoints[eip].push_back(key);
	}
	if (debuggerImpl.IsDebuggerConnected()) {

		uint16_t nameStrId = worker->getCurrentCallingLink().back().functionInfo->funcNameStrId;
		if (need_step_over.find(worker) != need_step_over.end() ||
			NeedBreakPoint(nameStrId, eip, packageId)) {

			platform.MutexLock(DebugGEL);

			need_step_over.erase(worker); //重置单步信号状态

			debuggerImpl.SendToDebugger("brk_trig");

			//复用GC暂停机制
			worker->VMInstance->currentGC->StopTheWorld();

			cur_breakPoint.key.eip = eip;
			cur_breakPoint.key.functionNameId = nameStrId;
			cur_breakPoint.key.packageId = packageId;
			cur_breakPoint.worker = worker;

			currentBreakState = BREAKED;

			while (currentBreakState == BREAKED) {
				platform.ThreadSleep(50); //等待时让出cpu的时间需要长一点，否则会触发看门狗
			}

			currentBreakState = NONE;

			worker->VMInstance->currentGC->ResumeTheWorld();

			platform.MutexUnlock(DebugGEL);
		}
	}
}


#endif