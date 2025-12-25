#include <stdint.h>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <stack>
#include "VariableValue.h"
#include "PlatformImpl.h"
#include "GC.h"


#define LESS_MEMORY_TRIG_COLLECT 0.2f  //达到GC触发阈值的内存
#define AUTO_GC_MIN_DELAY_MS 3000 //自动GC最小间隔
#define GC_MAX_WAIT_STW_TIME 500

void GC::enterSTWSafePoint() {
	platform.MutexLock(GCSTWCounterLock);
	STW_ArrivedThreadCount++; //世界暂停就绪线程计数器
	platform.MutexUnlock(GCSTWCounterLock);
	//利用阻塞锁等待GC回收完成
	platform.MutexLock(GCBlockEventLock);
	platform.MutexUnlock(GCBlockEventLock);

	platform.MutexLock(GCSTWCounterLock);
	STW_ArrivedThreadCount--; //减少计数，表示线程恢复运行
	platform.MutexUnlock(GCSTWCounterLock);
}

void GC::enterNativeFunc()
{
	platform.MutexLock(GCSTWCounterLock);
	EnteredNativeTaskCount++;
	platform.MutexUnlock(GCSTWCounterLock);
}

void GC::leaveNativeFunc()
{
	platform.MutexLock(GCSTWCounterLock);
	EnteredNativeTaskCount--;
	platform.MutexUnlock(GCSTWCounterLock);

	//从原生代码返回后检查GC是否正在进行，避免误修改栈
	if (GCRequired) {
		enterSTWSafePoint();
	}
}

void GC::StopTheWorld() {

	platform.MutexLock(GCSTWCounterLock);
	STW_ArrivedThreadCount++; //当前触发GC的线程被认为已经进入安全点，自增计数器
	platform.MutexUnlock(GCSTWCounterLock);
	
	platform.MutexLock(GCBlockEventLock); //开启安全点屏障阻塞Worker线程暂停世界
	GCRequired = true; //发布信号提醒其他线程需要GC
	

	//等待所有活的Worker线程进入安全点（不包括进入原生的线程）
	uint32_t aliveWorkerCount = 0;
	//对已经死亡的Worker执行清理，从vector移除并触发unique_ptr回收
	for (auto it = bindingVM->workers.begin(); it != bindingVM->workers.end();) {
		if ((*it)->getCallingLink().size() > 0) {
			aliveWorkerCount++;
			it++;
		}
		else {
			// 从vector中移除已完成的worker
			VMWorker* deadWorker = *it;
			deadWorker->~VMWorker();
			platform.MemoryFree(deadWorker);
			it = bindingVM->workers.erase(it);
		}
	}

	uint32_t waittingStart = platform.TickCount32();
	while (aliveWorkerCount > 0 && STW_ArrivedThreadCount < aliveWorkerCount - EnteredNativeTaskCount) {
		platform.ThreadYield();
	}
	//暂停完成
}

void GC::ResumeTheWorld() {

	platform.MutexLock(GCSTWCounterLock);
	STW_ArrivedThreadCount--; //取消当前线程就绪状态
	platform.MutexUnlock(GCSTWCounterLock);
	GCRequired = false;

	//释放锁恢复虚拟机世界运行
	platform.MutexUnlock(GCBlockEventLock);

	while (STW_ArrivedThreadCount > 0) {
		platform.ThreadYield(); //等待所有线程恢复
	}
}


GC::~GC() {
	platform.MutexDestroy(GCSTWCounterLock);
	platform.MutexDestroy(GCBlockEventLock);
	platform.MutexDestroy(GCObjectSetLock);
	platform.MutexDestroy(GCWorkLock);
	//GC被卸载时VM肯定不再运行了	
}

void GC::GCInit(VM* bindVM) {
	GCSTWCounterLock = platform.MutexCreate();
	GCBlockEventLock = platform.MutexCreate();
	GCObjectSetLock = platform.MutexCreate();
	GCWorkLock = platform.MutexCreate();
	bindingVM = bindVM;
}

VMObject* GC::GC_NewObject(ValueType::IValueType type) {
	//检测剩余内存，是否需要GC
	float freeMemoryPercent = platform.MemoryFreePercent();
	if (freeMemoryPercent < LESS_MEMORY_TRIG_COLLECT) {
		uint32_t currentTickTime = platform.TickCount32();
		uint32_t duration = currentTickTime - prevGCTime;
		//判断最近一次GC时间到现在间隔是否超过阈值
		if (duration > AUTO_GC_MIN_DELAY_MS) {
			//printf("run gc because %.2f free\n", freeMemoryPercent * 100);
			GC_Collect();
		}
	}

	VMObject* alloc = (VMObject*)platform.MemoryAlloc(sizeof(VMObject));
	new (alloc) VMObject(type); //调用构造函数
	platform.MutexLock(GCObjectSetLock);
	allObjects.insert(alloc);
	platform.MutexUnlock(GCObjectSetLock);
	return alloc;
}

VMObject* GC::GC_NewStringObject(std::string str) {
	VMObject* vmo = GC_NewObject(ValueType::STRING);
	vmo->implement.stringImpl = str;
	return vmo;
}

//手动回收对象
void GC::GC_DeleteObject(VMObject* vm_object) {
	platform.MutexLock(GCObjectSetLock);
	allObjects.erase(vm_object);
	platform.MutexUnlock(GCObjectSetLock);
	GC_FreeObject(vm_object);
}

//GC释放对象，外部非必要不要调用
void GC::GC_FreeObject(VMObject* vm_object) {
	vm_object->~VMObject(); //手动析构，因为C++直接回收内存不会自动析构
	platform.MemoryFree(vm_object);
}


//深度优先遍历对象引用图标记存活对象
void DFS_MarkObject(std::stack<VMObject*>& dfsStack,std::unordered_set<VMObject*>& allObjects,VM* VMInstance) {
	while (!dfsStack.empty()) {
		auto obj = dfsStack.top();
		dfsStack.pop();

		if (obj->marked) { //对象之间的引用是一个有环图，要做visited处理
			continue;
		}

		obj->marked = true;

		if (obj->type == ValueType::ARRAY) { //如果是数组就遍历数组引用标记
			//auto& vec = std::get<std::vector<VariableValue>>(obj->implement);
			auto& vec = obj->implement.arrayImpl;
			for (auto& variable : vec) {
				VariableValue* current = variable.getRawVariable();
				if (current->varType == ValueType::REF) {
					dfsStack.push(current->content.ref);
				}
			}
		}
		else if (obj->type == ValueType::OBJECT) {
			//auto& objmap = std::get<std::unordered_map<std::string, VariableValue>>(obj->implement);
			auto& objmap = obj->implement.objectImpl;
			for (auto& pair : objmap) {
				VariableValue* current = pair.second.getRawVariable();
				if (current->varType == ValueType::REF) {
					dfsStack.push(current->content.ref);
				}
			}
		}
		else if (obj->type == ValueType::FUNCTION) {
			//检查内置闭包对象和prop内联对象
			auto& funcImpl = obj->implement.closFuncImpl;
			//直接遍历这两个内置对象的Value段压栈
			if(funcImpl.closure) dfsStack.push(funcImpl.closure);
			if(funcImpl.propObject) dfsStack.push(funcImpl.propObject);

			//标记存活的包(这里确定携带闭包的对象一定是字节码函数)
			uint16_t pckId = funcImpl.sfn->funcImpl.local_func.packageId;
			VMInstance->loadedPackages[pckId].GCMarked = true;
		}
	}
}


//此方法检查线程安全和GC间隔

void GC::GC_Collect() {
	//printf("%d called\n", GetCurrentThreadId());
	bool locked = platform.MutexTryLock(GCWorkLock);
	printf("gc start %d\n", allObjects.size());
	if (!locked) {
		//printf("working!");
		return;
	}
	Internal_GC_Collect();
	printf("gc end %d\n", allObjects.size());
	platform.MutexUnlock(GCWorkLock);

	//printf(" gc end\n");
	//platform.ThreadSleep(1);
}

void GC::Internal_GC_Collect() {

	if (GCDisabled) {
		return;
	}

	prevGCTime = platform.TickCount32(); //更新上一次GC的时间

	uint32_t startTime = platform.TickCount32();
	uint32_t startObjectCount = allObjects.size();


	StopTheWorld();

	std::stack<VMObject*> dfsStack;

	//遍历全局符号表的所有引用对象

	for (auto& symbol : bindingVM->globalSymbols) {
		VariableValue* current = symbol.second.getRawVariable();
		if (current->varType == ValueType::REF) {
			dfsStack.push(symbol.second.getRawVariable()->content.ref);
		}
	}


	//遍历栈起点，将所有持有的局部变量设置为root
	for (auto worker : bindingVM->workers) {
		for (auto& fnFrame : worker->getCallingLink()) {

			//将调用链的所有字节码函数所属的包标记，运行中的字节码函数不应回收
			bindingVM->loadedPackages[fnFrame.functionInfo->packageId].GCMarked = true;

			for (auto& variable_ref : fnFrame.virtualStack) {
				if (variable_ref.varType == ValueType::REF) { //排除三个基本值类型其他都是引用
					//将局部变量引用的对象视作垃圾回收对象图根
					dfsStack.push(variable_ref.content.ref);
				}
				else if (variable_ref.varType == ValueType::FUNCTION &&
						variable_ref.content.function->type == ScriptFunction::Local) {
					//标记包内字节码函数对象
					uint16_t id = variable_ref.content.function->funcImpl.local_func.packageId;
					bindingVM->loadedPackages[id].GCMarked = true;
				}
			}
			//扫描局部变量
			for (auto& local_var : fnFrame.localVariables) {
				if (local_var.varType == ValueType::REF) {
					dfsStack.push(local_var.content.ref);
				}
				else if (local_var.varType == ValueType::FUNCTION &&
					local_var.content.function->type == ScriptFunction::Local) {
					//标记包内字节码函数对象
					uint16_t id = local_var.content.function->funcImpl.local_func.packageId;
					bindingVM->loadedPackages[id].GCMarked = true;
				}
			}
			//扫描局部环境符号表
			for (auto& env_pair : fnFrame.functionEnvSymbols) {
				if (env_pair.second.varType == ValueType::REF) {
					dfsStack.push(env_pair.second.content.ref);
				}
				else if (env_pair.second.varType == ValueType::FUNCTION &&
					env_pair.second.content.function->type == ScriptFunction::Local) {
					//标记包内字节码函数对象
					uint16_t id = env_pair.second.content.function->funcImpl.local_func.packageId;
					bindingVM->loadedPackages[id].GCMarked = true;
				}
			}
		}
	}

	//扫描可达对象
	DFS_MarkObject(dfsStack, allObjects,bindingVM);

	std::vector<VMObject*> finalizeQueue;
	//开始标记所有不可达但携带finalize方法的对象引用图，此时认为他还是有效对象
	for (VMObject* vmo : allObjects) {
		if (!vmo->marked && vmo->type == ValueType::OBJECT) {
			auto findFinalize = vmo->implement.objectImpl.find("finalize");
			if (findFinalize != vmo->implement.objectImpl.end()) {
				//这里使用getContentType判断是否是函数因为可能存在闭包的对象实现函数
				if ((*findFinalize).second.getContentType() == ValueType::FUNCTION) {
					//送入终结器调用队列，暂时不回收它的内存
					finalizeQueue.push_back(vmo);
					dfsStack.push(vmo); //标记他所有引用的对象，保证存活避免垂悬指针
					DFS_MarkObject(dfsStack, allObjects,bindingVM);
				}
			}
		}
	}

	//标记完有用的对象接下来删掉所有没用的

	for (auto it = allObjects.begin(); it != allObjects.end();) {
		VMObject* currentObject = *it;
		if (!currentObject->marked) {
			GC_FreeObject(currentObject);
			platform.MutexLock(GCObjectSetLock);
			it = allObjects.erase(it);
			platform.MutexUnlock(GCObjectSetLock);
			continue;
		}
		else {
			(*it)->marked = false; //存活的对象清理掉标记，下次回收无需重复清除标记
		}
		it++;
	}

	//清理已经无法访问的包，并将其卸载
	//先扫描所有的常量字符串，然后确认是否需要，并将标记重置
	for (auto& package : bindingVM->loadedPackages) {
		for (auto& con_str : package.second.ConstStringPool) {
			if (con_str->marked) {
				package.second.GCMarked = true;
				con_str->marked = false; //移除标记让下一次扫描
			}
		}
	}
	for (auto it = bindingVM->loadedPackages.begin(); it != bindingVM->loadedPackages.end();) {
		if ((*it).second.GCMarked) {
			(*it).second.GCMarked = false;
			it++;
		}
		else {
			it = bindingVM->UnloadPackageWithIterator(it);
		}
	}

	//清理完毕，恢复世界
	ResumeTheWorld();

#ifdef _DEBUG
	/*
	printf("清理前数量：%d  清理后数量：%d  一共清理：%d\n", count, allObjects.size(), count - allObjects.size());
	printf("Finalize Object Queue:\n");
	for (VMObject* needFinalizeObject : finalizeQueue) {
		wprintf("%ws\n",needFinalizeObject->ToString().c_str());
	}

	printf("=================\n");
	*/

#endif 

	//删完垃圾开始调用终结器

	for (VMObject* needFinalizeObject : finalizeQueue) {
		
		//ScriptFunction* finalizeFunc = needFinalizeObject->implement.objectImpl["finalize"].content.function;
		
		auto& finalizeFuncMember = needFinalizeObject->implement.objectImpl["finalize"];

		ScriptFunction* finalizeFunc;
		if (finalizeFuncMember.varType == ValueType::REF) {
			finalizeFunc = finalizeFuncMember.content.ref->implement.closFuncImpl.sfn;
		}
		else {
			finalizeFunc = finalizeFuncMember.content.function;
		}

		bool allowFinalize = true; //默认可以回收

		if (finalizeFunc->argumentCount == 0) {

			if (finalizeFunc->type == ScriptFunction::Local) {
				//auto result = InvokeBytecodeFinalizeFunc(finalizeFunc->funcImpl.local_func, needFinalizeObject);
				std::vector<VariableValue> args; //本身就没有参数，占位的
				auto result = bindingVM->InvokeCallback(finalizeFuncMember, args,needFinalizeObject);
				if (result.varType == ValueType::BOOL && result.content.boolean == false) {
					allowFinalize = false;
				}
			}
			else if (finalizeFunc->type == ScriptFunction::System) {

				VMWorker tempWorker(this->bindingVM);
				std::vector<VariableValue> args;
				auto result = finalizeFunc->funcImpl.system_func(args, needFinalizeObject, &tempWorker);
				if (result.varType == ValueType::BOOL && result.content.boolean == false) {
					allowFinalize = false;
				}
			}
			else {
				//do nothing
			}
		}
		else {
#ifdef _DEBUG
			printf("对象finalize方法签名不正确 %ws  参数数：%d\n", needFinalizeObject->ToString().c_str(), finalizeFunc->argumentCount);
#endif	
		}

		//移除finalize字段，这样下次就会被直接清理
		if (allowFinalize) {
			needFinalizeObject->implement.objectImpl.erase("finalize");
		}
	}

}

VariableValue GC::InvokeBytecodeFinalizeFunc(ByteCodeFunction& func,VMObject*thisValue) {
	VMWorker worker(bindingVM);
	FuncFrame frame;
	frame.byteCode = func.byteCode;
	frame.byteCodeLength = func.byteCodeLength;
	frame.functionInfo = &func;
	ScopeFrame defaultScope;
	//初始化默认作用域
	defaultScope.byteCodeLength = func.byteCodeLength;
	defaultScope.byteCodeStart = 0;
	defaultScope.ep = 0;
	defaultScope.spStart = 0;
	defaultScope.localvarStart = 0;
	frame.functionEnvSymbols["this"] = CreateReferenceVariable(thisValue);
	frame.scopeStack.push_back(defaultScope);
	worker.getCallingLink().push_back(frame);
	return worker.VMWorkerTask();
}