#pragma once

#include <stdint.h>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <stack>
#include "VM.h"


class GC {
private:
	VM* bindingVM; //绑定的虚拟机

	volatile uint32_t STW_ArrivedThreadCount = 0;
	//std::atomic<uint32_t> STW_ArrivedThreadCount{0};
	
	//**进入Native的线程返回后需要立即检查安全点，确保不会在GC期间修改操作栈导致未定义行为**
	volatile uint32_t EnteredNativeTaskCount = 0; //进入Native线程的数量
	//std::atomic<uint32_t> EnteredNativeTaskCount{0};

	std::unordered_set<VMObject*> allObjects;
	std::unordered_set<VMObject*> roots;

	uint32_t prevGCTime = 0;
	 
	

	void Internal_GC_Collect();

	VariableValue InvokeBytecodeFinalizeFunc(ByteCodeFunction& func, VMObject* thisValue);

public:

	bool GCDisabled = false;

	volatile bool GCRequired = false;
	
	//volatile bool GC_Working = false;

	void StopTheWorld();

	void ResumeTheWorld();

	void enterSTWSafePoint();

	//线程任务进入/退出原生代码后需要调用并检测是否需要GC安全点
	void enterNativeFunc();
	void leaveNativeFunc();

	//线程安全点计数器锁
	void* GCSTWCounterLock;

	void* GCWorkLock; //GC_Collect()函数被调用的线程安全锁

	//GC暂停世界安全点阻塞锁
	void* GCBlockEventLock;

	//并发修改对象表需要用到的锁
	void* GCObjectSetLock;

	~GC();

	void GCInit(VM* bindVM);
	VMObject* GC_NewObject(ValueType::IValueType type);
	VMObject* GC_NewStringObject(std::string str);
	void GC_DeleteObject(VMObject* vm_object);

	void GC_FreeObject(VMObject* vm_object);


	void GC_Collect();

	

	
};