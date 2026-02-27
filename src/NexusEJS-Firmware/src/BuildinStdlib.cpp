#include "BuildinStdlib.h"
#include "StringConverter.h"
#include <cstring>
#include <math.h>

VMObject* CreateTaskControlObject(uint32_t id, uint32_t threadId, VMWorker* worker,VariableValue& entryFunc);

//系统内置符号表
std::unordered_map<std::string, VariableValue> SystemBuildinSymbols;

//内置对象，销毁需要释放
std::vector<VMObject*> SystemStaticObjects;


//void* BuildinSymbolLock;

void RegisterSystemFunc(std::string name, uint8_t argCount, SystemFuncDef implement) {
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
	//VariableValue arg;
	int taskId = 0;
	VM* VMInstance = NULL;
}TaskStartParam;

void* TaskEntry(void* param) {
	TaskStartParam taskParam = *(TaskStartParam*)param;
	platform.MemoryFree(param); //拷贝完成后回收堆内存

	platform.MutexLock(taskParam.VMInstance->currentGC->GCWorkersVecLock);//上锁访问共享

	auto& context = taskParam.VMInstance->tasks[taskParam.taskId];
	context.status = TaskContext::RUNNING;
	context.worker = new VMWorker(taskParam.VMInstance);
	if (!context.worker) {
		platform.MutexUnlock(taskParam.VMInstance->currentGC->GCWorkersVecLock);
		return NULL; //分配失败静默失败
	}
	

	platform.MutexUnlock(taskParam.VMInstance->currentGC->GCWorkersVecLock);

	//开始调用脚本函数
	std::vector<VariableValue> paramList;
	context.worker->currentWorkerId = taskParam.taskId;
	//自动注册到VM并Call过去
	auto res = taskParam.VMInstance->InvokeCallbackWithWorker(context.worker, context.function, paramList, NULL);
	
	//处理返回值
	platform.MutexLock(taskParam.VMInstance->currentGC->GCWorkersVecLock);

	context = taskParam.VMInstance->tasks[taskParam.taskId]; //重新获取避免地址变化
	if(context.TaskObject) context.TaskObject->implement.objectImpl["_ret"] = res;
	context.status = TaskContext::STOPED;

	platform.MutexUnlock(taskParam.VMInstance->currentGC->GCWorkersVecLock);

	return NULL;
}

VMObject* CreateTaskControlObject(uint32_t id, uint32_t threadId, VMWorker* worker,VariableValue& entryFunc) {
	VMObject* vmo = worker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT,VMObject::PROTECTED);
	vmo->implement.objectImpl["id"] = CreateNumberVariable(id);
	vmo->implement.objectImpl["threadId"] = CreateNumberVariable(threadId);
	vmo->implement.objectImpl["isRunning"] = TaskObj_isRunningFunc;
	vmo->implement.objectImpl["waitTimeout"] = TaskObj_waitTimeoutFunc;
	vmo->implement.objectImpl["getResult"] = TaskObj_getResultFunc;

	vmo->implement.objectImpl["finalize"] = TaskObj_finalizeFunc;

	//任务对象引用函数避免被回收
	vmo->implement.objectImpl["entry"] = entryFunc;

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

//Buffer对象容器模板，创建时拷贝到新对象
std::unordered_map<std::string, VariableValue> BufferObjTemplate;

ByteBufferInfo GetByteBufferInfo(uint32_t bufid) {
	return bindedBytebuffer[bufid];
}

//初始化单例模板
void InitByteBufferSingleFunc() {
	
// fill(val, offset, size)
BufferObjTemplate["fill"] = VM::CreateSystemFunc(3, 
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].varType != ValueType::NUM || 
            args[1].varType != ValueType::NUM || 
            args[2].varType != ValueType::NUM) {
            currentWorker->ThrowError("invaild argument");
            return VariableValue();
        }
        
        uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
        auto& info = bindedBytebuffer[id];
        
        // 参数提取
        uint8_t fillValue = (uint8_t)args[0].content.number;  // 填充值
        uint32_t offset = (uint32_t)args[1].content.number;   // 起始偏移
        uint32_t size = (uint32_t)args[2].content.number;      // 填充大小
        
        // 边界检查
        if (offset >= info.length) {
            currentWorker->ThrowError("offset out of range");
            return VariableValue();
        }
        
        if (size == 0) {
            // 大小为0，什么都不做
            return VariableValue();
        }
        
        // 计算实际要填充的大小
        uint32_t actualSize = size;
        if (offset + size > info.length) {
            // 如果超出范围，只填充到末尾
            actualSize = info.length - offset;
        }
        
        // 使用memset进行填充
        memset(info.data + offset, fillValue, actualSize);
        
        return VariableValue();
    });

	//readUInt(offset,size);
	BufferObjTemplate["readUInt"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}
		uint64_t integerBuffer = 0;


		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
		});
	BufferObjTemplate["readInt"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}
		int64_t integerBuffer = 0; //有符号版本


		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
		});
	//readFloat(offset)
	BufferObjTemplate["readFloat"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = sizeof(float);
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}
		float integerBuffer = 0;

		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable((double)integerBuffer);
		});
	BufferObjTemplate["readDouble"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = sizeof(double);
		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}
		double integerBuffer = 0;

		memcpy(&integerBuffer, info.data + offset, size);
		return CreateNumberVariable(integerBuffer);
		});
	//writeUInt(offset,size,value)
	BufferObjTemplate["writeUInt"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM || args[2].varType != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		uint64_t value = (uint64_t)args[2].content.number;

		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	BufferObjTemplate["writeInt"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM || args[2].varType != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		uint32_t size = (uint32_t)args[1].content.number;
		int64_t value = (int64_t)args[2].content.number;

		if (offset < 0 || offset >= info.length || offset + size > info.length || size <= 0 || size > 8) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	BufferObjTemplate["writeFloat"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		float value = (float)args[1].content.number;
		uint32_t size = sizeof(float);

		if (offset < 0 || offset >= info.length || offset + size > info.length) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	BufferObjTemplate["writeDouble"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument");
			return VariableValue();
		}
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;
		double value = args[1].content.number;
		uint32_t size = sizeof(double);

		if (offset < 0 || offset >= info.length || offset + size > info.length) {
			currentWorker->ThrowError("out of range");
			return VariableValue();
		}

		memcpy(info.data + offset, &value, size);
		return VariableValue();
		});
	// readUTF8(offset, maxBytes?) - 读取UTF-8字符串，maxBytes可选
	BufferObjTemplate["readUTF8"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 1 || args.size() > 2) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError("first argument must be number");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		// 计算maxBytes 如果提供了就使用，否则读取到缓冲区尾部
		uint32_t maxBytes = 0;
		if (args.size() >= 2) {
			if (args[1].getContentType() != ValueType::NUM) {
				currentWorker->ThrowError("second argument must be number");
				return VariableValue();
			}
			maxBytes = (uint32_t)args[1].content.number;
		}
		else {
			// 没有提供maxBytes，读取到缓冲区尾部
			maxBytes = info.length - offset;
		}

		if (offset >= info.length) {
			currentWorker->ThrowError("out of bounds");
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

			return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject("",VMObject::PROTECTED));
		}

		std::string utf8_str((char*)info.data + offset, strLength);

		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(utf8_str,VMObject::PROTECTED));
		});

	// writeUTF8(offset, string, addNull?) - 写入UTF-8字符串，addNull可选
	BufferObjTemplate["writeUTF8"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 2 || args.size() > 3) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError("first argument must be number");
			return VariableValue();
		}

		if (args[1].getContentType() != ValueType::STRING) {
			currentWorker->ThrowError("second argument must be string");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		std::string utf8_str = args[1].content.ref->implement.stringImpl;

		// 是否添加null结束符
		bool addNull = true;
		if (args.size() >= 3) {
			if (args[2].getContentType() != ValueType::BOOL) {
				currentWorker->ThrowError("third argument must be boolean");
				return VariableValue();
			}
			addNull = (bool)args[2].content.boolean;
		}

		size_t byteLength = utf8_str.length() + (addNull ? 1 : 0);

		if (offset >= info.length || offset + byteLength > info.length) {
			currentWorker->ThrowError("out of bounds");
			return VariableValue();
		}

		memcpy(info.data + offset, utf8_str.c_str(), utf8_str.length());
		if (addNull) {
			info.data[offset + utf8_str.length()] = 0;
		}

		return VariableValue();
		});

	// readUTF16(offset, length?) - 读取UTF-16字符串，length可选
	BufferObjTemplate["readUTF16"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 1 || args.size() > 2) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError("first argument must be number");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		//如果提供了就使用，否则读取到缓冲区尾部
		uint32_t length = 0;
		if (args.size() >= 2) {
			if (args[1].getContentType() != ValueType::NUM) {
				currentWorker->ThrowError("second argument must be number");
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
			currentWorker->ThrowError("out of bounds");
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

		//转换成内部utf8
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(wstring_to_string(result),VMObject::PROTECTED));
		});

	// writeUTF16(offset, string, addNull?) - 写入UTF-16字符串，addNull可选
	BufferObjTemplate["writeUTF16"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args.size() < 2 || args.size() > 3) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		if (args[1].getContentType() != ValueType::STRING) {
			currentWorker->ThrowError("invalid argument count");
			return VariableValue();
		}

		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		auto& info = bindedBytebuffer[id];
		uint32_t offset = (uint32_t)args[0].content.number;

		std::string u8str = args[1].content.ref->implement.stringImpl;

		//转换UTF-16字符串
		std::wstring str = string_to_wstring(u8str);

		size_t length = str.length();

		// 是否添加null结束符
		bool addNull = true;
		if (args.size() >= 3) {
			if (args[2].getContentType() != ValueType::BOOL) {
				currentWorker->ThrowError("third argument must be boolean");
				return VariableValue();
			}
			addNull = (bool)args[2].content.boolean;
		}

		size_t byteLength = length * 2 + (addNull ? 2 : 0);

		if (offset >= info.length || offset + byteLength > info.length) {
			currentWorker->ThrowError("out of bounds");
			return VariableValue();
		}

		// 写入字符串
		memcpy(info.data + offset, str.c_str(), length * 2);
		if (addNull) {
			char nullTerm = 0;
			memcpy(info.data + offset + length * 2, &nullTerm, 2);
		}

		return VariableValue();
		});

	//析构

	BufferObjTemplate["close"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		printf("ByteBuffer.close id:%d\n", id);
		auto& info = bindedBytebuffer[id];
		platform.MemoryFree(info.data);
		bindedBytebuffer.erase(id);
		thisValue->implement.objectImpl.erase("finalize"); //删掉析构函数让GC可直接回收
		return VariableValue();
		});

	BufferObjTemplate["finalize"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		uint32_t id = (uint32_t)thisValue->implement.objectImpl["bufid"].content.number;
		printf("ByteBuffer.finalize id:%d\n", id);
		auto& info = bindedBytebuffer[id];
		platform.MemoryFree(info.data);
		bindedBytebuffer.erase(id);
		return CreateBooleanVariable(true);
		});

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
	VMObject* vmo = worker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT,VMObject::PROTECTED);

	vmo->implement.objectImpl = BufferObjTemplate; //拷贝内置成员方法

	vmo->implement.objectImpl["bufid"] = CreateNumberVariable((double)id);

	vmo->implement.objectImpl["size"] = CreateNumberVariable((double)size);

	for (auto& pair : vmo->implement.objectImpl) {
		pair.second.readOnly = true;
	}
	return vmo;
}

#pragma endregion

#pragma region MathImpl

void MathClassInit() {
	VMObject* mathClass = CreateStaticObject(ValueType::OBJECT);
	mathClass->implement.objectImpl["sin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			worker->ThrowError("invaild argument type");
		}
		return CreateNumberVariable(sin(args[0].content.number));
	}); mathClass->implement.objectImpl["sin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sin(args[0].content.number));
		});

	mathClass->implement.objectImpl["cos"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(cos(args[0].content.number));
		});

	mathClass->implement.objectImpl["tan"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(tan(args[0].content.number));
		});

	mathClass->implement.objectImpl["asin"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < -1.0 || x > 1.0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(asin(x));
		});

	mathClass->implement.objectImpl["acos"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < -1.0 || x > 1.0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(acos(x));
		});

	mathClass->implement.objectImpl["atan"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(atan(args[0].content.number));
		});

	mathClass->implement.objectImpl["atan2"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(atan2(args[0].content.number, args[1].content.number));
		});

	mathClass->implement.objectImpl["sqrt"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x < 0) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sqrt(x));
		});

	mathClass->implement.objectImpl["pow"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(pow(args[0].content.number, args[1].content.number));
		});

	mathClass->implement.objectImpl["exp"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(exp(args[0].content.number));
		});

	mathClass->implement.objectImpl["log"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
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

	mathClass->implement.objectImpl["log10"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
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

	mathClass->implement.objectImpl["abs"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(fabs(args[0].content.number));
		});

	mathClass->implement.objectImpl["ceil"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(ceil(args[0].content.number));
		});

	mathClass->implement.objectImpl["floor"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(floor(args[0].content.number));
		});

	mathClass->implement.objectImpl["round"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(round(args[0].content.number));
		});

	mathClass->implement.objectImpl["max"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double a = args[0].content.number;
		double b = args[1].content.number;
		return CreateNumberVariable(a > b ? a : b);
		});

	mathClass->implement.objectImpl["min"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM || args[1].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double a = args[0].content.number;
		double b = args[1].content.number;
		return CreateNumberVariable(a < b ? a : b);
		});

	mathClass->implement.objectImpl["random"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		static volatile bool inited = false;
		if (!inited) {
			inited = true;
			srand(platform.TickCount32()); //启动时间作为种子
		}
		
		return CreateNumberVariable((double)rand() / RAND_MAX);
		});

	mathClass->implement.objectImpl["PI"] = CreateNumberVariable(3.14159265358979323846);
	mathClass->implement.objectImpl["E"] = CreateNumberVariable(2.71828182845904523536);

	// 常量
	mathClass->implement.objectImpl["SQRT2"] = CreateNumberVariable(1.41421356237309504880);
	mathClass->implement.objectImpl["SQRT1_2"] = CreateNumberVariable(0.70710678118654752440);
	mathClass->implement.objectImpl["LN2"] = CreateNumberVariable(0.69314718055994530942);
	mathClass->implement.objectImpl["LN10"] = CreateNumberVariable(2.30258509299404568402);
	mathClass->implement.objectImpl["LOG2E"] = CreateNumberVariable(1.44269504088896340736);
	mathClass->implement.objectImpl["LOG10E"] = CreateNumberVariable(0.43429448190325182765);

	// 三角函数（弧度转角度）
	mathClass->implement.objectImpl["degrees"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(args[0].content.number * 180.0 / 3.14159265358979323846);
		});

	mathClass->implement.objectImpl["radians"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(args[0].content.number * 3.14159265358979323846 / 180.0);
		});

	// 双曲函数
	mathClass->implement.objectImpl["sinh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(sinh(args[0].content.number));
		});

	mathClass->implement.objectImpl["cosh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(cosh(args[0].content.number));
		});

	mathClass->implement.objectImpl["tanh"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(tanh(args[0].content.number));
		});

	// 符号函数
	mathClass->implement.objectImpl["sign"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		double x = args[0].content.number;
		if (x > 0) return CreateNumberVariable(1);
		if (x < 0) return CreateNumberVariable(-1);
		return CreateNumberVariable(0);
		});

	// 取余
	mathClass->implement.objectImpl["fmod"] = VM::CreateSystemFunc(2, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
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
	mathClass->implement.objectImpl["trunc"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].varType != ValueType::NUM) {
			return CreateNumberVariable(NAN);
		}
		return CreateNumberVariable(trunc(args[0].content.number));
		});

	for (auto& pair : mathClass->implement.objectImpl) {
		pair.second.readOnly = true;
	}

	SystemBuildinSymbols["Math"] = CreateReferenceVariable(mathClass);
}

#pragma endregion

#pragma region ObjectClassImpl

void ObjectClassInit() {
	VMObject* objectClass = CreateStaticObject(ValueType::OBJECT);
	objectClass->implement.objectImpl["keys"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].getContentType() != ValueType::OBJECT) {
			worker->ThrowError("invaild argument");
			return VariableValue();
		}
		VMObject* keys = worker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
		for (auto& pair : args[0].content.ref->implement.objectImpl) {
			VMObject* strObject = worker->VMInstance->currentGC->GC_NewStringObject(pair.first);
			keys->implement.arrayImpl.push_back(CreateReferenceVariable(strObject));
		}

		return CreateReferenceVariable(keys);
		});

	objectClass->implement.objectImpl["values"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].getContentType() != ValueType::OBJECT) {
			worker->ThrowError("invaild argument");
			return VariableValue();
		}
		VMObject* values = worker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
		for (auto& pair : args[0].content.ref->implement.objectImpl) {
			values->implement.arrayImpl.push_back(pair.second);
		}

		return CreateReferenceVariable(values);
		});

	objectClass->implement.objectImpl["entries"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {
		if (args[0].getContentType() != ValueType::OBJECT) {
			worker->ThrowError("invalid argument");
			return VariableValue();
		}

		VMObject* entriesArray = worker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);

		for (auto& pair : args[0].content.ref->implement.objectImpl) {
			//[key, value]
			VMObject* entryArray = worker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);

			VMObject* keyObject = worker->VMInstance->currentGC->GC_NewStringObject(pair.first);

			entryArray->implement.arrayImpl.push_back(CreateReferenceVariable(keyObject));
			entryArray->implement.arrayImpl.push_back(pair.second);

			entriesArray->implement.arrayImpl.push_back(CreateReferenceVariable(entryArray));
		}

		return CreateReferenceVariable(entriesArray);
		});

	SystemBuildinSymbols["Object"] = CreateReferenceVariable(objectClass);
}

#pragma endregion

//全局单例注册（非符号表内的SystemFunction）
void SingleSystemFuncInit() {

	/***下面项初始化后存储都必须要在SingleSystemFunctionStore中存储指针***/
	TaskObj_isRunningFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {
		platform.MutexLock(worker->VMInstance->currentGC->GCWorkersVecLock);
		uint32_t taskId = (uint32_t)thisValue->implement.objectImpl["id"].content.number;
		bool res = worker->VMInstance->tasks[taskId].status == TaskContext::RUNNING;
		platform.MutexUnlock(worker->VMInstance->currentGC->GCWorkersVecLock);
		return CreateBooleanVariable(res);
		});
	//SingleSystemFunctionStore.push_back(TaskObj_isRunningFunc.content.function);

	TaskObj_waitTimeoutFunc = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) { //用户可能传入进来其他类型
			worker->ThrowError("only support number");
			return VariableValue();
		}
		uint32_t targetTime = (uint32_t)args[0].content.number;

		auto& objContainer = thisValue->implement.objectImpl;

		platform.MutexLock(worker->VMInstance->currentGC->GCWorkersVecLock);
		auto& context = worker->VMInstance->tasks[(uint32_t)objContainer["id"].content.number];
		platform.MutexUnlock(worker->VMInstance->currentGC->GCWorkersVecLock);

		uint32_t start = platform.TickCount32();

		worker->VMInstance->currentGC->IgnoreWorkerCount_Inc();
		while (context.status == TaskContext::RUNNING) {
			uint32_t duration = platform.TickCount32() - start;
			if (duration > targetTime || duration < 0) { //如果溢出直接视作超时
				worker->VMInstance->currentGC->IgnoreWorkerCount_Dec();
				return CreateBooleanVariable(false);
			}
			platform.ThreadSleep(1);
		}
		worker->VMInstance->currentGC->IgnoreWorkerCount_Dec();
		return CreateBooleanVariable(true);
		});
	//SingleSystemFunctionStore.push_back(TaskObj_waitTimeoutFunc.content.function);

	TaskObj_getResultFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker) -> VariableValue {	
		return thisValue->implement.objectImpl["_ret"];
	});
	//SingleSystemFunctionStore.push_back(TaskObj_getResultFunc.content.function);


	TaskObj_finalizeFunc = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* worker)->VariableValue {

		uint16_t taskId = (uint32_t)thisValue->implement.objectImpl["id"].content.number;

		if (worker->VMInstance->tasks[taskId].status == TaskContext::RUNNING) {
#ifdef _DEBUG
			//wprintf("TaskObject.finalize() false %ws\n", thisValue->ToString().c_str());
#endif
			return CreateBooleanVariable(false); //返回false表示控制对象还不能销毁
		}
		platform.MutexLock(worker->VMInstance->currentGC->GCWorkersVecLock);
		worker->VMInstance->tasks.erase(taskId);
		platform.MutexUnlock(worker->VMInstance->currentGC->GCWorkersVecLock);
#ifdef _DEBUG
		//wprintf("TaskObject.finalize() true %ws\n", thisValue->ToString().c_str());
#endif
		return CreateBooleanVariable(true);
	});
	//SingleSystemFunctionStore.push_back(TaskObj_finalizeFunc.content.function);

	InitByteBufferSingleFunc();
}


void VTAPI_Init() {
	//虚拟线程启动API
	RegisterSystemFunc("vtStart", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].getContentType() != ValueType::FUNCTION) {
			currentWorker->ThrowError("vtStart(entry) only accept Func type with 1 arg.");
			return VariableValue();
		}
		ScriptFunction* targetInvoke;
		if (args[0].varType == ValueType::REF) {
			targetInvoke = args[0].content.ref->implement.closFuncImpl.sfn;
		}
		else {
			targetInvoke = args[0].content.function;
		}

		if (targetInvoke->type != ScriptFunction::Local) {
			currentWorker->ThrowError("task must be local function.");
			return VariableValue();
		}

		if (targetInvoke->argumentCount != 0) {
			currentWorker->ThrowError("entry must has 0 arg");
			return VariableValue();
		}

		currentWorker->getAllVTBlocks().emplace_back(args[0]);

		return VariableValue();
		});

	RegisterSystemFunc("vtDelay", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].varType != ValueType::NUM) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}

		//没有其他虚拟线程可调度或者调度器被禁用时退回到操作系统的sleep
		if (currentWorker->getAllVTBlocks().size() <= 1 || (!currentWorker->getVTScheduleEnabled())) {
			platform.ThreadSleep((uint32_t)args[0].content.number);
		}
		else {
			//让出CPU让调度器调度其他虚拟线程
			auto& vtBlock = currentWorker->getCurrentVTBlock();
			vtBlock.vtStatus = VirtualThreadSchedBlock::BLOCKING;
			vtBlock.awakeTime = platform.TickCount32() + args[0].content.number;
			currentWorker->vtScheduleNext();
		}

		return VariableValue();
		});

	RegisterSystemFunc("vtSetScheduleEnabled", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {

		if (args[0].varType != ValueType::BOOL) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}

		//if(args[0].content.boolean)
		currentWorker->setVTScheduleEnabled(args[0].content.boolean);

		return VariableValue();

		});
}

void BuildinStdlib_Init()
{
	//BuildinSymbolLock = platform.MutexCreate();

	SingleSystemFuncInit();

	VTAPI_Init();

	RegisterSystemFunc("runTask", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		//return VariableValue();

		VM* currentVM = currentWorker->VMInstance;

		//修改workers表要确保线程安全，同一时间只有一个线程修改workers和context表

		if (args[0].getContentType() != ValueType::FUNCTION) {
			currentWorker->ThrowError("runTask(entry) only accept Func type with 1 arg.");
			return VariableValue();
		}

		ScriptFunction* targetInvoke;
		if (args[0].varType == ValueType::REF) {
			targetInvoke = args[0].content.ref->implement.closFuncImpl.sfn;
		}
		else {
			targetInvoke = args[0].content.function;
		}
		//args[0].getRawVariable()->content.function;

		if (targetInvoke->type != ScriptFunction::Local) {
			currentWorker->ThrowError("task must be local function.");
			return VariableValue();
		}

		if (targetInvoke->argumentCount != 0) {
			currentWorker->ThrowError("entry must has 0 arg");
			return VariableValue();
		}

		

		uint32_t currentTaskId = currentVM->lastestTaskId++;
		
		TaskContext context;
		context.id = currentTaskId;
		//context.worker = new VMWorker(currentWorker->VMInstance);
		context.processEntry = TaskEntry;
		context.function = args[0];

		//fix: 如果是函数对象（一般都是）那就添加保护，稍后返回的时候会被统一销毁标记
		if (context.function.varType == ValueType::REF) {
			context.function.content.ref->protectStatus = VMObject::PROTECTED;
		}
		
		platform.MutexLock(currentWorker->VMInstance->currentGC->GCWorkersVecLock);
		currentVM->tasks[currentTaskId] = context;
		platform.MutexUnlock(currentWorker->VMInstance->currentGC->GCWorkersVecLock);

		TaskStartParam* taskParam = (TaskStartParam*)platform.MemoryAlloc(sizeof(TaskStartParam));

		taskParam->taskId = currentTaskId;
		taskParam->VMInstance = currentVM;
		//taskParam->arg = *args[1].getRawVariable();

		uint32_t threadId = platform.StartThread(context.processEntry, taskParam);

		if (!threadId) {
			platform.MemoryFree(taskParam);
			//currentVM->workers.pop_back();
			return VariableValue(); //失败返回NULLREF
		}

		currentVM->tasks[currentTaskId].threadId = threadId;

		currentVM->tasks[currentTaskId].status = TaskContext::RUNNING;

		VMObject* TaskControlObject = CreateTaskControlObject(
			context.id,
			context.threadId,
			currentWorker,
			args[0]
		);

		currentVM->tasks[currentTaskId].TaskObject = TaskControlObject;

		return CreateReferenceVariable(TaskControlObject);
	});

	RegisterSystemFunc("mutexLock", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {
		
		if (args[0].getRawVariable()->varType != ValueType::REF) {
			currentWorker->ThrowError("lock only support object value");
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

	RegisterSystemFunc("mutexUnlock", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {

		

		if (args[0].getRawVariable()->varType != ValueType::REF) {
			currentWorker->ThrowError("lock only support object value");
			return VariableValue();
		}

		VMObject* vmo = args[0].content.ref;
		//printf("mutexUnlock(%p)",vmo->mutex);
		platform.MutexUnlock(vmo->mutex);
		return VariableValue();
	});
	
	RegisterSystemFunc("require", 1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {

		if (args[0].getContentType() != ValueType::STRING) {
			currentWorker->ThrowError("invaild argument");
			return VariableValue();
		}

		if (!platform.FileExist(args[0].content.ref->implement.stringImpl)) {
			currentWorker->ThrowError("file dos not exist");
			return VariableValue();
		}
		uint32_t fileSize = 0;
		uint8_t* nejsBuffer = platform.ReadFile(args[0].content.ref->implement.stringImpl,&fileSize);

		if (!nejsBuffer || fileSize == 0) {
			currentWorker->ThrowError("fail to read the nejs file");
			if (nejsBuffer) {
				platform.MemoryFree(nejsBuffer);
				return VariableValue();
			}
		}
		
		uint16_t id = currentWorker->VMInstance->LoadPackedProgram(nejsBuffer, fileSize);

		if (id == 0) {
			currentWorker->ThrowError("invaild nejs package");
			platform.MemoryFree(nejsBuffer);
			return VariableValue();
		}
		
		platform.MemoryFree(nejsBuffer); //释放读取文件分配的内存

		auto& package = currentWorker->VMInstance->loadedPackages[id];
		
		VariableValue ret; //失败的默认返回值

		auto find = package.bytecodeFunctions.find("main_entry");

		if (find != package.bytecodeFunctions.end()) {
			std::vector<VariableValue> args;
			ret = currentWorker->VMInstance->InvokeCallback((*find).second, args, NULL);
		}

		return ret;

		});

	MathClassInit();

	ObjectClassInit();

	VMObject* NumberClassObject = CreateStaticObject(ValueType::OBJECT);
	NumberClassObject->implement.objectImpl["parseFloat"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		char* endPtr = NULL;
		double result = strtod(args[0].ToString().c_str(), &endPtr);
		return CreateNumberVariable(result);
	});
	NumberClassObject->implement.objectImpl["parseInt"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		char* endPtr = NULL;
		double result = strtod(args[0].ToString().c_str(), &endPtr);
		return CreateNumberVariable(result);
	});
	NumberClassObject->implement.objectImpl["toString"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(args[0].ToString(),VMObject::PROTECTED));
	});
	SystemBuildinSymbols["Number"] = CreateReferenceVariable(NumberClassObject);

	VMObject* BufferClassObject = CreateStaticObject(ValueType::OBJECT);
	//Buffer.create(size)
	BufferClassObject->implement.objectImpl["create"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].getContentType() != ValueType::NUM) {
			currentWorker->ThrowError("invalid argument");
			return VariableValue();
		}
		uint32_t size = args[0].content.number;
		VMObject* BufferObject = CreateByteBufferObject(size, currentWorker);
		if (!BufferObject) {
			currentWorker->ThrowError("no enough memory");
			return VariableValue();
		}
		return CreateReferenceVariable(BufferObject);
	});

	SystemBuildinSymbols["Buffer"] = CreateReferenceVariable(BufferClassObject);
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

	//批量释放对象
	for (auto& pair : SystemBuildinSymbols) {
		if (pair.second.varType == ValueType::REF) {
			pair.second.content.ref->~VMObject();
			platform.MemoryFree(pair.second.content.ref); //这里移除了非函数类引用
		}
	} 

	SystemBuildinSymbols.clear();
	/*
	for (ScriptFunction* system_func : SingleSystemFunctionStore) {
		platform.MemoryFree(system_func);
	}
	*/

	//释放所有系统函数（值函数类型）
	for (auto sfn : VM::SystemFunctionObjects) {
		sfn->~ScriptFunction();
		platform.MemoryFree(sfn);
	}
}

VariableValue* SystemGetSymbol(std::string& symbol)
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
