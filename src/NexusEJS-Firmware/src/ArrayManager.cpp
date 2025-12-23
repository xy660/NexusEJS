#include "ArrayMapager.h"
#include "PlatformImpl.h"
#include <stdint.h>
#include <functional>
#include <algorithm>

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

std::unordered_map<std::string, VariableValue> array_symbol_map;
std::vector<ScriptFunction*> arrayman_script_function_alloc;

void* ArraySymbolMapLock;

void ArraySymbolFuncAdd(std::string name,SystemFuncDef func,uint16_t argumentCount) {
	
	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (fnref.content.function) ScriptFunction(ScriptFunction::System);
	arrayman_script_function_alloc.push_back(fnref.content.function);
	fnref.content.function->argumentCount = argumentCount;
	fnref.content.function->funcImpl.system_func = func;
	array_symbol_map[name] = fnref;
	
}

static void __CallArrayCallback(VariableValue& callback,VariableValue& ArrayRef,VMWorker* worker,std::function<bool(VariableValue,uint32_t)> onResult) {
    //创建临时worker
    VMWorker tempWorker(worker->VMInstance);

    auto& array = ArrayRef.content.ref->implement.arrayImpl;

    auto sfn = callback.content.ref->implement.closFuncImpl.sfn;

    for (size_t i = 0; i < array.size(); i++) {
        std::vector<VariableValue> callback_args;
        if (sfn->argumentCount == 1) {
            callback_args.push_back(array[i]);
        }
        else if(sfn->argumentCount == 3) {
            callback_args.push_back(array[i]);
            callback_args.push_back(CreateNumberVariable((double)i));
            callback_args.push_back(ArrayRef);
        }
        auto res = worker->VMInstance->InvokeCallbackWithTempWorker(&tempWorker, callback, callback_args, ArrayRef.content.ref);
        if (!onResult(res, i)) {
            break;
        }
    }
}

void ArrayManager_Init() { 

	ArraySymbolMapLock = platform.MutexCreate();

	ArraySymbolFuncAdd("push", [](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue {
		if (thisValue->type != ValueType::ARRAY) {
			return VariableValue();
		}

		//复制传入的值加入容器实现持久化
		thisValue->implement.arrayImpl.push_back(*args[0].getRawVariable());

		VariableValue ret;
		ret.varType = ValueType::REF;
		ret.content.ref = thisValue;
		return ret;
		},1);

	//STORE存储会丢失Bridge被归一化到最终值类型进行赋值，因此这个bridge仅在arr[i] = xxx这一条有效
	ArraySymbolFuncAdd("get", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker)->VariableValue {
		if (thisValue->type != ValueType::ARRAY || args[0].getContentType() != ValueType::NUM) {
			return VariableValue();
		}

		uint32_t index = (uint32_t)args[0].content.number;

		if (index < 0 || index >= thisValue->implement.arrayImpl.size()) {
			return VariableValue();
		}

		VariableValue ret;
		ret.varType = ValueType::BRIDGE;
		ret.content.bridge_ref = &thisValue->implement.arrayImpl[index];
		return ret;
		},1);


    // pop - 移除最后一个元素并返回
    ArraySymbolFuncAdd("pop", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || thisValue->implement.arrayImpl.empty()) {
            return VariableValue();
        }
        VariableValue ret = thisValue->implement.arrayImpl.back();
        thisValue->implement.arrayImpl.pop_back();
        return ret;
        }, 0);

    // length - 获取数组长度
    ArraySymbolFuncAdd("length", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            VariableValue ret;
            ret.varType = ValueType::NUM;
            ret.content.number = 0;
            return ret;
        }
        VariableValue ret;
        ret.varType = ValueType::NUM;
        ret.content.number = (double)thisValue->implement.arrayImpl.size();
        return ret;
        }, 0);

    // shift - 移除第一个元素并返回
    ArraySymbolFuncAdd("shift", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || thisValue->implement.arrayImpl.empty()) {
            return VariableValue();
        }
        VariableValue ret = thisValue->implement.arrayImpl.front();
        thisValue->implement.arrayImpl.erase(thisValue->implement.arrayImpl.begin());
        return ret;
        }, 0);

    // unshift - 在开头添加元素
    ArraySymbolFuncAdd("unshift", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }
        for (size_t i = args.size(); i > 0; --i) {
            thisValue->implement.arrayImpl.insert(thisValue->implement.arrayImpl.begin(), *args[i - 1].getRawVariable());
        }
        VariableValue ret;
        ret.varType = ValueType::REF;
        ret.content.ref = thisValue;
        return ret;
        }, DYNAMIC_ARGUMENT);

    // indexOf - 查找元素索引
    ArraySymbolFuncAdd("indexOf", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty()) {
            VariableValue ret;
            ret.varType = ValueType::NUM;
            ret.content.number = -1;
            return ret;
        }

        size_t fromIndex = 0;
        if (args.size() > 1 && args[1].getContentType() == ValueType::NUM) {
            fromIndex = (size_t)args[1].content.number;
        }

        auto& array = thisValue->implement.arrayImpl;
        for (size_t i = fromIndex; i < array.size(); i++) {
            // 简单比较，实际需要更完善的比较逻辑
            if (array[i] == args[0]) {
                return CreateNumberVariable((double)i);
            }
        }

        return CreateNumberVariable(-1);

        }, DYNAMIC_ARGUMENT);

    // join - 连接数组元素为字符串
    ArraySymbolFuncAdd("join", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }

        std::string separator = ",";
        if (!args.empty() && args[0].getContentType() == ValueType::STRING) {
            separator = args[0].content.ref->implement.stringImpl;
        }

        std::string result;
        auto& array = thisValue->implement.arrayImpl;
        for (size_t i = 0; i < array.size(); i++) {
            if (i > 0) {
                result += separator;
            }
            result += array[i].ToString();
        }

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
        }, DYNAMIC_ARGUMENT);

    // slice - 返回数组片段
    ArraySymbolFuncAdd("slice", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;
        size_t start = 0;
        size_t end = array.size();

        if (args.size() > 0 && args[0].getContentType() == ValueType::NUM) {
            start = (size_t)args[0].content.number;
        }

        if (args.size() > 1 && args[1].getContentType() == ValueType::NUM) {
            end = (size_t)args[1].content.number;
        }

        if (start < 0) start = 0;
        if (end > array.size()) end = array.size();
        if (start > end) start = end;

        //创建新数组
        VMObject* newArray = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);

        for (size_t i = start; i < end; i++) {
            newArray->implement.arrayImpl.push_back(array[i]);
        }

        return CreateReferenceVariable(newArray);
        }, DYNAMIC_ARGUMENT);

    // reverse - 反转数组
    ArraySymbolFuncAdd("reverse", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;
        std::reverse(array.begin(), array.end());

        return CreateReferenceVariable(thisValue);
        }, 0);

    // forEach - 遍历数组
    ArraySymbolFuncAdd("forEach", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return VariableValue();
        }

        if (args[0].varType != ValueType::REF || args[0].content.ref->type != ValueType::FUNCTION) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;
        
        //原始数组的引用，也就是thisValue
        auto ArrayRef = CreateReferenceVariable(thisValue);
        __CallArrayCallback(args[0], ArrayRef, currentWorker, [](VariableValue res,uint32_t index) {
            return true;
        });

        return VariableValue(); // 返回undefined
        },1);

    // map - 映射新数组
    ArraySymbolFuncAdd("map", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;
        VMObject* newArray = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        //原始数组的引用，也就是thisValue
        auto ArrayRef = CreateReferenceVariable(thisValue);
        __CallArrayCallback(args[0], ArrayRef, currentWorker, [newArray](VariableValue res,uint32_t index) {

            newArray->implement.arrayImpl.push_back(res);

            return true;
        });

        return CreateReferenceVariable(newArray);
        },1);

    // filter - 过滤数组
    ArraySymbolFuncAdd("filter", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;
        VMObject* newArray = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        
        auto ArrayRef = CreateReferenceVariable(thisValue);

        __CallArrayCallback(args[0], ArrayRef, currentWorker, [newArray,array](VariableValue res,uint32_t index) {

            if (res.varType == ValueType::BOOL && res.content.boolean) {
                newArray->implement.arrayImpl.push_back(array[index]);
            }

            return true;
        });

        return CreateReferenceVariable(newArray);
        },1);

    // find - 查找元素
    ArraySymbolFuncAdd("find", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;

        auto ArrayRef = CreateReferenceVariable(thisValue);

        int resultIndex = -1;

        __CallArrayCallback(args[0], ArrayRef, currentWorker, [array,&resultIndex](VariableValue res, uint32_t index) {

            if (res.varType == ValueType::BOOL && res.content.boolean) {
                resultIndex = index;
                return false;
            }
            return true;
        });
        if (resultIndex != -1) {
            return array[resultIndex];
        }
        else {
            return VariableValue();
        }
        },1);


    // some - 是否有元素满足条件
    ArraySymbolFuncAdd("some", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return CreateBooleanVariable(false);
        }

        auto& array = thisValue->implement.arrayImpl;
        auto ArrayRef = CreateReferenceVariable(thisValue);

        bool found = false;
        __CallArrayCallback(args[0], ArrayRef, currentWorker, [&found](VariableValue res, uint32_t index) {
            if (res.varType == ValueType::BOOL && res.content.boolean) {
                found = true;
                return false;
            }
            return true;
            });

        return CreateBooleanVariable(found);
        }, 1);

    // every - 所有元素是否满足条件
    ArraySymbolFuncAdd("every", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY || args.empty() ||
            args[0].getContentType() != ValueType::FUNCTION) {
            return CreateBooleanVariable(false);
        }

        auto& array = thisValue->implement.arrayImpl;
        if (array.empty()) {
            return CreateBooleanVariable(true);
        }

        auto ArrayRef = CreateReferenceVariable(thisValue);

        bool allMatch = true;
        __CallArrayCallback(args[0], ArrayRef, currentWorker, [&allMatch](VariableValue res, uint32_t index) {
            if (res.varType != ValueType::BOOL || !res.content.boolean) {
                allMatch = false;
                return false;
            }
            return true;
            });

        return CreateBooleanVariable(allMatch);
        }, 1);

    

    // concat - 连接数组
    ArraySymbolFuncAdd("concat", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }

        VMObject* newArray = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);

        for (const auto& item : thisValue->implement.arrayImpl) {
            newArray->implement.arrayImpl.push_back(item);
        }

        for (size_t i = 0; i < args.size(); i++) {
            if (args[i].getContentType() == ValueType::ARRAY) {
                VMObject* argArray = args[i].content.ref;
                for (const auto& subItem : argArray->implement.arrayImpl) {
                    newArray->implement.arrayImpl.push_back(subItem);
                }
            }
            else {
                newArray->implement.arrayImpl.push_back(args[i]);
            }
        }

        return CreateReferenceVariable(newArray);
        }, DYNAMIC_ARGUMENT);

    // splice - 删除/替换元素
    ArraySymbolFuncAdd("splice", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return VariableValue();
        }

        auto& array = thisValue->implement.arrayImpl;

        uint32_t start = 0;
        if (args.size() > 0 && args[0].getContentType() == ValueType::NUM) {
            start = (uint32_t)args[0].content.number;
            if (start < 0) start = 0;
            if (start > (uint32_t)array.size()) start = array.size();
        }

        uint32_t deleteCount = array.size() - start;
        if (args.size() > 1 && args[1].getContentType() == ValueType::NUM) {
            deleteCount = (uint32_t)args[1].content.number;
            if (deleteCount < 0) deleteCount = 0;
            if (deleteCount > (uint32_t)(array.size() - start)) {
                deleteCount = array.size() - start;
            }
        }

        VMObject* deletedArray = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        for (uint32_t i = 0; i < deleteCount && (start + i) < (uint32_t)array.size(); i++) {
            deletedArray->implement.arrayImpl.push_back(array[start + i]);
        }

        std::vector<VariableValue> newElements;
        for (size_t i = 2; i < args.size(); i++) {
            newElements.push_back(args[i]);
        }

        if (deleteCount > 0) {
            array.erase(array.begin() + start, array.begin() + start + deleteCount);
        }

        for (uint32_t i = 0; i < (uint32_t)newElements.size(); i++) {
            array.insert(array.begin() + start + i, newElements[i]);
        }

        return CreateReferenceVariable(deletedArray);
        }, 2);


    // sort - 数组排序
    ArraySymbolFuncAdd("sort", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (thisValue->type != ValueType::ARRAY) {
            return CreateReferenceVariable(thisValue);
        }

        auto& array = thisValue->implement.arrayImpl;

        if (array.size() <= 1) {
            return CreateReferenceVariable(thisValue);
        }

        // 检查是否有比较函数
        bool hasCompare = false;
        VariableValue compareFunc;

        if (!args.empty() && args[0].getContentType() == ValueType::FUNCTION) {
            hasCompare = true;
            compareFunc = args[0];
        }

        std::sort(array.begin(), array.end(),
            [currentWorker, thisValue, hasCompare, &compareFunc](const VariableValue& a, const VariableValue& b) -> bool {
                if (!hasCompare) {
                    // 默认转换为字符串比较
                    std::string strA = a.ToString();
                    std::string strB = b.ToString();
                    return strA < strB;
                }

                std::vector<VariableValue> callbackArgs = { a, b };
                VMWorker tempWorker(currentWorker->VMInstance);
                VariableValue result = currentWorker->VMInstance->InvokeCallbackWithTempWorker(&tempWorker, compareFunc, callbackArgs, thisValue);

                //将结果转数字
                double cmp = 0.0;
                if (result.getContentType() == ValueType::NUM) {
                    cmp = result.content.number;
                }
                else if (result.getContentType() == ValueType::BOOL) {
                    cmp = result.content.boolean ? 1.0 : 0.0;
                }
                else if (result.getContentType() == ValueType::STRING) {
                    const char* str = result.content.ref->implement.stringImpl.c_str();
                    char* end = nullptr;
                    cmp = std::strtod(str, &end);
                    if (end == str) cmp = 0.0; //转换失败
                }
                // 其他类型都当作0

                // 如果比较函数返回 < 0，a应该在b前面
                return cmp < 0;
            });

        return CreateReferenceVariable(thisValue);
        }, DYNAMIC_ARGUMENT);
}

void ArrayManager_Destroy() {
	platform.MutexDestroy(ArraySymbolMapLock);

	for (auto& pair : array_symbol_map) {
		if (pair.second.varType == ValueType::FUNCTION) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
	}

	array_symbol_map.clear();
}

VariableValue GetArraySymbol(std::string& symbol,VMObject* owner) {
	//对动态字段的特殊处理
	if (symbol == "length") {
		VariableValue ret;
		ret.varType = ValueType::NUM;
		ret.content.number = owner->implement.arrayImpl.size();
		return ret;
	}

	platform.MutexLock(ArraySymbolMapLock);

	if (array_symbol_map.find(symbol) == array_symbol_map.end()) {
		platform.MutexUnlock(ArraySymbolMapLock);
		return VariableValue(); //如果不存在就返回NULLREF，避免破坏
	}
	
	//复制并设置thisValue
	VariableValue ret = array_symbol_map[symbol];
	ret.thisValue = owner;
	platform.MutexUnlock(ArraySymbolMapLock);
	return ret;

}
