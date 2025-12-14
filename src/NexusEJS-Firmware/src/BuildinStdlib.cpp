#include "BuildinStdlib.h"
#include "StringConverter.h"
#include <cstring>
#include <math.h>

VMObject* CreateTaskControlObject(uint32_t id, uint32_t threadId, VMWorker* worker);

//系统内置符号表
std::unordered_map<std::wstring, VariableValue> SystemBuildinSymbols;

//内置对象，销毁需要释放
std::vector<VMObject*> SystemStaticObjects;

//存储一些不常驻符号表的系统函数对象，比如TaskObject中的操作方法
std::vector<ScriptFunction*> SingleSystemFunctionStore;

//void* BuildinSymbolLock;

void RegisterSystemFunc(std::wstring name, uint8_t argCount, SystemFuncDef implement) {
	auto funcRef = VM::CreateSystemFunc(argCount, implement); 
	funcRef.readOnly = true;
	SystemBuildinSymbols[name] = funcRef;
}

VMObject* CreateStaticObject(ValueType::IValueType type) {
	VMObject* vmo = (VMObject*)platform.MemoryAlloc(sizeof(VMObject));
	new (vmo) VMObject(type);
	SystemStaticObjects.push_back(vmo);
	return vmo;
}

#pragma region RunTaskMethod

//初始化单例系统函数
VariableValue TaskObj_isRunningFunc;

VariableValue TaskObj_waitTimeoutFunc;

VariableValue TaskObj_getResultFunc;

VariableValue TaskObj_finalizeFunc;


typedef struct _TaskStartParam
{
	VariableValue arg;
	int taskId = 0;
	VM* VMInstance = NULL;
}TaskStartParam;

void* TaskEntry(void* param) {
	TaskStartParam taskParam = *(TaskStartParam*)param;
	platform.MemoryFree(param); //拷贝完成后回收堆内存

	platform.MutexLock(taskParam.VMInstance->globalSymbolLock);//上锁访问共享

	auto& context = taskParam.VMInstance->tasks[taskParam.taskId];
	context.status = TaskContext::RUNNING;
	//std::unordered_map<std::wstring, VariableValue> paramList;
	//paramList[context.sfn->arguments[0]] = taskParam.arg; //拷贝第一个也是唯一一个参数
	std::vector<VariableValue> paramList;
	paramList.push_back(taskParam.arg);

	platform.MutexUnlock(taskParam.VMInstance->globalSymbolLock);

	context.worker->currentWorkerId = taskParam.taskId;
	auto res = context.worker->Init(*context.sfn,paramList);

	platform.MutexLock(taskParam.VMInstance->globalSymbolLock); 

	context = taskParam.VMInstance->tasks[taskParam.taskId]; //重新获取避免地址变化
	context.result = res;
	context.status = TaskContext::STOPED;

	platform.MutexUnlock(taskParam.VMInstance->globalSymbolLock);

	return NULL;
}

VMObject* CreateTaskControlObject(uint32_t id, uint32_t threadId, VMWorker* worker) {
	VMObject* vmo = worker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
	vmo->implement.objectImpl[L"id"] = CreateNumberVariable(id);
	vmo->implement.objectImpl[L"threadId"] = CreateNumberVariable(threadId);
	vmo->implement.objectImpl[L"isRunning"] = TaskObj_isRunningFunc;
	vmo->implement.objectImpl[L"waitTimeout"] = TaskObj_waitTimeoutFunc;
	vmo->implement.objectImpl[L"getResult"] = TaskObj_getResultFunc;

	vmo->implement.objectImpl[L"finalize"] = TaskObj_finalizeFunc;
	for (auto& pair : vmo->implement.objectImpl) {
		pair.second.readOnly = true; // 设置只读防止用户乱赋值导致内存泄漏
	}
	return vmo;
}


#pragma endregion

#pragma region ByteBufferImpl


//脚本层绑定的byteBuffer
std::unordered_map<uint32_t, ByteBufferInfo> bindedBytebuffer;
uint32_t byteBufferIdSeed = 1;

ByteBufferInfo GetByteBufferInfo(uint32_t bufid) {
	return bindedBytebuffer[bufid];
}

//失败返回NULL
VMObject* CreateByteBufferObject(uint32_t size,VMWorker* worker) {
	
	ByteBufferInfo info;
	info.data = (uint8_t*)platform.MemoryAlloc(size);
	if (!info.data) {
		return NULL;
	}
	uint32_t id = byteBufferIdSeed++;
	info.length = size;
	bindedBytebuffer[id] = info;
	VMObject* vmo = worker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
	vmo->implement.objectImpl[L"bufid"] = CreateNumberVariable((double)id);
	//readUInt(offset,size);
	vmo->implement.objectImpl[L"readUInt"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}
		uint64_t integerBuffer = 0;
		

		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
	});
	vmo->implement.objectImpl[L"readInt"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}
		int64_t integerBuffer = 0; //有符号版本


		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
	});
	//readFloat(offset)
	vmo->implement.objectImpl[L"readFloat"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = sizeof(float);
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}
		float integerBuffer = 0;

		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
	});
	vmo->implement.objectImpl[L"readDouble"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = sizeof(double);
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}
		double integerBuffer = 0;

		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable(integerBuffer);
		});
	//writeUInt(offset,size,value)
	vmo->implement.objectImpl[L"writeUInt"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM || args[2].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		uint64_t value = (uint64_t)args[2].content.number;

		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	vmo->implement.objectImpl[L"writeInt"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM || args[2].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		int64_t value = (int64_t)args[2].content.number;

		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	vmo->implement.objectImpl[L"writeFloat"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		float value = (float)args[1].content.number;
		uint32_t size = sizeof(float);

		if (offset < 0 || offset >= info.length || offset + size > info.length) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	vmo->implement.objectImpl[L"writeDouble"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		double value = args[1].content.number;
		uint32_t size = sizeof(double);

		if (offset < 0 || offset >= info.length || offset + size > info.length) {
			currentWorker->ThrowError(L"out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	// readUTF8(offset, maxBytes?) - 读取UTF-8字符串，maxBytes可选
	vmo->implement.objectImpl[L"readUTF8"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 1 || args.size() > 2) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError(L"first argument must be number");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		// 计算maxBytes 如果提供了就使用，否则读取到缓冲区尾部
		uint32_t maxBytes = 0;
		if (args.size() >= 2) {
			if (args[1].getContentType() != ValueType::NUM) {
				currentWorker->ThrowError(L"second argument must be number");
				return VariableValue();
			}
			maxBytes = (uint32_t)args[1].content.number;
		}
		else {
			// 没有提供maxBytes，读取到缓冲区尾部
			maxBytes = info.length - offset;
		}

		if (offset >= info.length) {
			currentWorker->ThrowError(L"out of bounds");
			return VariableValue();
		}

		// 计算实际要读取的字节数
		uint32_t bytesToRead = maxBytes;
		if (offset + maxBytes > info.length) {
			bytesToRead = info.length - offset;
		}

		// 找到字符串结束符
		uint32_t strLength = 0;
		while (strLength < bytesToRead && info.data[offset + strLength] != 0) {
			strLength++;
		}

		if (strLength == 0) {

			return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(L""));
		}

		// 转换为UTF-8字符串
		std::string utf8_str((char*)info.data + offset, strLength);

		// 使用系统提供的转换函数
		std::wstring utf16_str = string_to_wstring(utf8_str);
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(utf16_str));
		});

	// writeUTF8(offset, string, addNull?) - 写入UTF-8字符串，addNull可选
	vmo->implement.objectImpl[L"writeUTF8"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 2 || args.size() > 3) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError(L"first argument must be number");
			return VariableValue();
		}

		if (args[1].getContentType() != ValueType::STRING) {
			currentWorker->ThrowError(L"second argument must be string");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		// 获取UTF-16字符串
		std::wstring utf16_str = args[1].content.ref->implement.stringImpl;
		std::string utf8_str = wstring_to_string(utf16_str);

		// 是否添加null结束符
		bool addNull = true;
		if (args.size() >= 3) {
			if (args[2].getContentType() != ValueType::BOOL) {
				currentWorker->ThrowError(L"third argument must be boolean");
				return VariableValue();
			}
			addNull = (bool)args[2].content.boolean;
		}

		size_t byteLength = utf8_str.length() + (addNull ? 1 : 0);

		if (offset >= info.length || offset + byteLength > info.length) {
			currentWorker->ThrowError(L"out of bounds");
			return VariableValue();
		}

		memcpy(info.data + offset, utf8_str.c_str(), utf8_str.length());
		if (addNull) {
			info.data[offset + utf8_str.length()] = 0;
		}

		return VariableValue();
		});

	// readUTF16(offset, length?) - 读取UTF-16字符串，length可选
	vmo->implement.objectImpl[L"readUTF16"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 1 || args.size() > 2) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError(L"first argument must be number");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		//如果提供了就使用，否则读取到缓冲区尾部
		uint32_t length = 0;
		if (args.size() >= 2) {
			if (args[1].getContentType() != ValueType::NUM) {
				currentWorker->ThrowError(L"second argument must be number");
				return VariableValue();
			}
			length = (uint32_t)args[1].content.number;
		}
		else {
			//没有提供length，读取到缓冲区尾部
			length = (info.length - offset) / 2;
		}

		uint32_t byteLength = length * 2;

		if (offset >= info.length || offset + byteLength > info.length) {
			currentWorker->ThrowError(L"out of bounds");
			return VariableValue();
		}

		// 分配内存创建字符串
		wchar_t* buffer = (wchar_t*)(info.data + offset);

		// 查找字符串结束符
		uint32_t actualLength = 0;
		for (uint32_t i = 0; i < length; i++) {
			if (buffer[i] == 0) {
				break;
			}
			actualLength++;
		}

		std::wstring result(buffer, actualLength);
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
		});

	// writeUTF16(offset, string, addNull?) - 写入UTF-16字符串，addNull可选
	vmo->implement.objectImpl[L"writeUTF16"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 2 || args.size() > 3) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		if (args[1].getContentType() != ValueType::STRING) {
			currentWorker->ThrowError(L"invalid argument count");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		// 获取UTF-16字符串
		std::wstring str = args[1].content.ref->implement.stringImpl;
		size_t length = str.length();

		// 是否添加null结束符
		bool addNull = true;
		if (args.size() >= 3) {
			if (args[2].getContentType() != ValueType::BOOL) {
				currentWorker->ThrowError(L"third argument must be boolean");
				return VariableValue();
			}
			addNull = (bool)args[2].content.boolean;
		}

		size_t byteLength = length * 2 + (addNull ? 2 : 0);

		if (offset >= info.length || offset + byteLength > info.length) {
			currentWorker->ThrowError(L"out of bounds");
			return VariableValue();
		}

		// 写入字符串
		memcpy(info.data + offset, str.c_str(), length * 2);
		if (addNull) {
			wchar_t nullTerm = 0;
			memcpy(info.data + offset + length * 2, &nullTerm, 2);
		}

		return VariableValue();
		});

	//析构

	vmo->implement.objectImpl[L"finalize"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		uint32_t id = (uint32_t)thisValue->implement.objectImpl[L"bufid"].content.number;
		printf("ByteBuffer.finalize id:%d\n", id);
		auto& info = bindedBytebuffer[id];
		platform.MemoryFree(info.data);
		bindedBytebuffer.erase(id);
		return CreateBooleanVariable(true);
	});
	

	for (auto& pair : vmo->implement.objectImpl) {
		pair.second.readOnly = true;
	}
	return vmo;
}

#pragma endregion

#pragma region MathImpl

void MathClassInit() {
	VMObject* mathClass = CreateStaticObject(ValueType::OBJECT);
	mathClass->implement.objectImpl[L"sin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			worker->ThrowError(L"invaild argument type");
		}
		return CreateNumberVariable(sin(args[0].content.number));
	}); mathClass->implement.objectImpl[L"sin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sin(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"cos"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(cos(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"tan"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(tan(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"asin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < -1.0 || x > 1.0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(asin(x));
		});

	mathClass->implement.objectImpl[L"acos"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < -1.0 || x > 1.0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(acos(x));
		});

	mathClass->implement.objectImpl[L"atan"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(atan(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"atan2"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(atan2(args[0].content.number, args[1].content.number));
		});

	mathClass->implement.objectImpl[L"sqrt"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < 0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sqrt(x));
		});

	mathClass->implement.objectImpl[L"pow"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(pow(args[0].content.number, args[1].content.number));
		});

	mathClass->implement.objectImpl[L"exp"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(exp(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"log"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < 0) {
			return CreateNumberVariable(NAN);
		}
		if (x == 0) {
			return CreateNumberVariable(-INFINITY);
		}
		return CreateNumberVariable(log(x));
		});

	mathClass->implement.objectImpl[L"log10"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < 0) {
			return CreateNumberVariable(NAN);
		}
		if (x == 0) {
			return CreateNumberVariable(-INFINITY);
		}
		return CreateNumberVariable(log10(x));
		});

	mathClass->implement.objectImpl[L"abs"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(fabs(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"ceil"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(ceil(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"floor"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(floor(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"round"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(round(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"max"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double a = args[0].content.number;
		double b = args[1].content.number;
		return CreateNumberVariable(a > b ? a : b);
		});

	mathClass->implement.objectImpl[L"min"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double a = args[0].content.number;
		double b = args[1].content.number;
		return CreateNumberVariable(a < b ? a : b);
		});

	mathClass->implement.objectImpl[L"random"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		return CreateNumberVariable((double)rand() / RAND_MAX);
		});

	mathClass->implement.objectImpl[L"PI"] = CreateNumberVariable(3.14159265358979323846);
	mathClass->implement.objectImpl[L"E"] = CreateNumberVariable(2.71828182845904523536);

	// 常量
	mathClass->implement.objectImpl[L"SQRT2"] = CreateNumberVariable(1.41421356237309504880);
	mathClass->implement.objectImpl[L"SQRT1_2"] = CreateNumberVariable(0.70710678118654752440);
	mathClass->implement.objectImpl[L"LN2"] = CreateNumberVariable(0.69314718055994530942);
	mathClass->implement.objectImpl[L"LN10"] = CreateNumberVariable(2.30258509299404568402);
	mathClass->implement.objectImpl[L"LOG2E"] = CreateNumberVariable(1.44269504088896340736);
	mathClass->implement.objectImpl[L"LOG10E"] = CreateNumberVariable(0.43429448190325182765);

	// 三角函数（弧度转角度）
	mathClass->implement.objectImpl[L"degrees"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(args[0].content.number * 180.0 / 3.14159265358979323846);
		});

	mathClass->implement.objectImpl[L"radians"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(args[0].content.number * 3.14159265358979323846 / 180.0);
		});

	// 双曲函数
	mathClass->implement.objectImpl[L"sinh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sinh(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"cosh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(cosh(args[0].content.number));
		});

	mathClass->implement.objectImpl[L"tanh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(tanh(args[0].content.number));
		});

	// 符号函数
	mathClass->implement.objectImpl[L"sign"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x > 0) return CreateNumberVariable(1);
		if (x < 0) return CreateNumberVariable(-1);
		return CreateNumberVariable(0);
		});

	// 取余
	mathClass->implement.objectImpl[L"fmod"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double a = args[0].content.number;
		double b = args[1].content.number;
		if (b == 0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(fmod(a, b));
		});

	// 截断
	mathClass->implement.objectImpl[L"trunc"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(trunc(args[0].content.number));
		});

	for (auto& pair : mathClass->implement.objectImpl) {
		pair.second.readOnly = true;
	}

	SystemBuildinSymbols[L"Math"] = CreateReferenceVariable(mathClass);
}

#pragma endregion

//全局单例注册（非符号表内的SystemFunction）
void SingleSystemFuncInit() {

	/***下面项初始化后存储都必须要在SingleSystemFunctionStore中存储指针***/
	TaskObj_isRunningFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {
		platform.MutexLock(worker->VMInstance->globalSymbolLock);
		uint32_t taskId = (uint32_t)thisValue->implement.objectImpl[L"id"].content.number;
		bool res = worker->VMInstance->tasks[taskId].status == TaskContext::RUNNING;
		platform.MutexUnlock(worker->VMInstance->globalSymbolLock);
		return CreateBooleanVariable(res);
		});
	SingleSystemFunctionStore.push_back(TaskObj_isRunningFunc.content.function);

	TaskObj_waitTimeoutFunc = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) { //用户可能传入进来其他类型
			worker->ThrowError(L"only support number");
			return VariableValue();
		}
		uint32_t targetTime = (uint32_t)args[0].content.number;

		auto& objContainer = thisValue->implement.objectImpl;

		platform.MutexLock(worker->VMInstance->globalSymbolLock);
		auto& context = worker->VMInstance->tasks[(uint32_t)objContainer[L"id"].content.number];
		platform.MutexUnlock(worker->VMInstance->globalSymbolLock);

		uint32_t start = platform.TickCount32();

		while (context.status == TaskContext::RUNNING) {
			uint32_t duration = platform.TickCount32() - start;
			if (duration > targetTime || duration < 0) { //如果溢出直接视作超时
				return CreateBooleanVariable(false);
			}
			platform.ThreadSleep(1);
		}
		return CreateBooleanVariable(true);
		});
	SingleSystemFunctionStore.push_back(TaskObj_waitTimeoutFunc.content.function);

	TaskObj_getResultFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {
		platform.MutexLock(worker->VMInstance->globalSymbolLock);
		uint32_t taskId = (uint32_t)thisValue->implement.objectImpl[L"id"].content.number;
		auto result = worker->VMInstance->tasks[taskId].result;
		platform.MutexUnlock(worker->VMInstance->globalSymbolLock);
		return result;
	});
	SingleSystemFunctionStore.push_back(TaskObj_getResultFunc.content.function);


	TaskObj_finalizeFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {

		uint16_t taskId = (uint32_t)thisValue->implement.objectImpl[L"id"].content.number;

		if (worker->VMInstance->tasks[taskId].status == TaskContext::RUNNING) {
#ifdef _DEBUG
			//wprintf(L"TaskObject.finalize() false %ws\n", thisValue->ToString().c_str());
#endif
			return CreateBooleanVariable(false); //返回false表示控制对象还不能销毁
		}

		worker->VMInstance->tasks.erase(taskId);
#ifdef _DEBUG
		//wprintf(L"TaskObject.finalize() true %ws\n", thisValue->ToString().c_str());
#endif
		return CreateBooleanVariable(true);
	});
	SingleSystemFunctionStore.push_back(TaskObj_finalizeFunc.content.function);

	
}


void BuildinStdlib_Init()
{
	//BuildinSymbolLock = platform.MutexCreate();

	SingleSystemFuncInit();

	RegisterSystemFunc(L"runTask", 2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		//return VariableValue();

		VM* currentVM = currentWorker->VMInstance;

		//修改workers表要确保线程安全，同一时间只有一个线程修改workers和context表

		if (args[0].getContentType() != ValueType::FUNCTION) {
			currentWorker->ThrowError(L"runTask(entry,param) only accept Func type with 1 arg.");
			return VariableValue();
		}

		ScriptFunction* targetInvoke = args[0].getRawVariable()->content.function;

		if (targetInvoke->type != ScriptFunction::Local) {
			currentWorker->ThrowError(L"task must be local function.");
			return VariableValue();
		}

		if (targetInvoke->argumentCount != 1) {
			currentWorker->ThrowError(L"entry must has 1 arg");
			return VariableValue();
		}

		

		platform.MutexLock(currentVM->globalSymbolLock);

		uint32_t currentTaskId = currentVM->lastestTaskId++;

		currentVM->workers.push_back(std::unique_ptr<VMWorker>(new VMWorker(currentVM)));
		

		TaskContext context;
		context.id = currentTaskId;
		context.worker = currentVM->workers.back().get();
		context.processEntry = TaskEntry;
		context.sfn = &targetInvoke->funcImpl.local_func;
		

		TaskStartParam* taskParam = (TaskStartParam*)platform.MemoryAlloc(sizeof(TaskStartParam));

		taskParam->taskId = currentTaskId;
		taskParam->VMInstance = currentVM;
		taskParam->arg = *args[1].getRawVariable();

		uint32_t threadId = platform.StartThread(context.processEntry, taskParam);

		if (!threadId) {
			platform.MemoryFree(taskParam);
			currentVM->workers.pop_back();
			return VariableValue(); //失败返回NULLREF
		}

		context.threadId = threadId;

		context.status = TaskContext::RUNNING;

		currentVM->tasks[currentTaskId] = context;

		platform.MutexUnlock(currentVM->globalSymbolLock);

		return CreateReferenceVariable(CreateTaskControlObject(
			context.id,
			context.threadId,
			currentWorker
		));
	});

	RegisterSystemFunc(L"mutexLock", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {
		
		if (args[0].getRawVariable()->varType != ValueType::REF) {
			currentWorker->ThrowError(L"lock only support object value");
			return VariableValue();
		}

		VMObject* vmo = args[0].content.ref;
		if (!vmo->mutex) {
			vmo->mutex = platform.MutexCreate();
		}
		//printf("mutexLock(%p)", vmo->mutex);
		platform.MutexLock(vmo->mutex);
		return VariableValue();
	});

	RegisterSystemFunc(L"mutexUnlock", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {

		

		if (args[0].getRawVariable()->varType != ValueType::REF) {
			currentWorker->ThrowError(L"lock only support object value");
			return VariableValue();
		}

		VMObject* vmo = args[0].content.ref;
		//printf("mutexUnlock(%p)",vmo->mutex);
		platform.MutexUnlock(vmo->mutex);
		return VariableValue();
	});

	VMObject* NumberClassObject = CreateStaticObject(ValueType::OBJECT);
	NumberClassObject->implement.objectImpl[L"parseFloat"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		wchar_t* endPtr = NULL;
		double result = wcstod(args[0].ToString().c_str(), &endPtr);
		return CreateNumberVariable(result);
	});
	NumberClassObject->implement.objectImpl[L"parseInt"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		wchar_t* endPtr = NULL;
		double result = wcstod(args[0].ToString().c_str(), &endPtr);
		return CreateNumberVariable(result);
	});
	NumberClassObject->implement.objectImpl[L"toString"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(args[0].ToString()));
	});
	SystemBuildinSymbols[L"Number"] = CreateReferenceVariable(NumberClassObject);

	VMObject* BufferClassObject = CreateStaticObject(ValueType::OBJECT);
	//Buffer.create(size)
	BufferClassObject->implement.objectImpl[L"create"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError(L"invalid argument");
			return VariableValue();
		}
		uint32_t size = args[0].content.number;
		VMObject* BufferObject = CreateByteBufferObject(size, currentWorker);
		if (!BufferObject) {
			currentWorker->ThrowError(L"no enough memory");
			return VariableValue();
		}
		return CreateReferenceVariable(BufferObject);
	});

	SystemBuildinSymbols[L"Buffer"] = CreateReferenceVariable(BufferClassObject);
}

void BuildinStdlib_Destroy() {
	//platform.MutexDestroy(BuildinSymbolLock);

	//这里不需要释放了 下面释放过了，之前忘了所以注释掉，下次需要再启用
	/*
	for (auto vmo : SystemStaticObjects) {
		vmo->~VMObject();
		platform.MemoryFree(vmo);
	}
	*/

	for (auto& pair : SystemBuildinSymbols) {
		if (pair.second.varType == ValueType::FUNCTION) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
		else if (pair.second.varType == ValueType::REF) {
			pair.second.content.ref->~VMObject();
			platform.MemoryFree(pair.second.content.ref); //这里移除了非函数类引用
		}
	} 

	SystemBuildinSymbols.clear();

	for (ScriptFunction* system_func : SingleSystemFunctionStore) {
		platform.MemoryFree(system_func);
	}
}

VariableValue* SystemGetSymbol(std::wstring& symbol)
{
	//platform.MutexLock(BuildinSymbolLock);
	if (SystemBuildinSymbols.find(symbol) == SystemBuildinSymbols.end()) {
		//platform.MutexUnlock(BuildinSymbolLock);
		return NULL;
	}
	VariableValue* ret = &SystemBuildinSymbols[symbol];
	//platform.MutexUnlock(BuildinSymbolLock);
	
	return ret;
}
