#pragma once

#include<stdint.h>

typedef void* (*MemoryAllocDef)(uint32_t size);
typedef void (*MemoryFreeDef)(void* pHeap);
typedef void* (*ThreadEntry)(void* param);
typedef uint32_t (*StartThreadDef)(ThreadEntry entry, void* param);
typedef uint32_t (*CurrentThreadIdDef)();
typedef uint32_t (*TickCount32Def)();
typedef void (*ThreadYieldDef)();
typedef void(*ThreadSleepDef)(uint32_t sleep_ms);
typedef void* (*MutexCreateDef)();
typedef void (*MutexLockDef)(void* mutex);
typedef bool (*MutexTryLockDef)(void* mutex);
typedef void (*MutexUnlockDef)(void* mutex);
typedef void (*MutexDestroyDef)(void* mutex);
typedef float (*MemoryFreePercentDef)();

//目标平台需要实现的抽象函数
class PlatformImpl
{
public:
	MemoryAllocDef MemoryAlloc;
	MemoryFreeDef MemoryFree;
	StartThreadDef StartThread;
	CurrentThreadIdDef CurrentThreadId;
	ThreadYieldDef ThreadYield;
	ThreadSleepDef ThreadSleep;
	TickCount32Def TickCount32; 
	MutexCreateDef MutexCreate;
	MutexLockDef MutexLock;
	MutexTryLockDef MutexTryLock;
	MutexUnlockDef MutexUnlock;
	MutexDestroyDef MutexDestroy;
	MemoryFreePercentDef MemoryFreePercent; //返回系统可用内存百分比 0.0-1.0
};

//初始化虚拟机之前必须填充platform依赖的原生函数
extern PlatformImpl platform;