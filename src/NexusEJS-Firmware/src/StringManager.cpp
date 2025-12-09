#include "StringManager.h"
#include "PlatformImpl.h"
#include <stdint.h>
#include <algorithm>
#include "VM.h"

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

std::unordered_map<std::wstring, VariableValue> string_symbol_map;
std::vector<ScriptFunction*> stringman_script_function_alloc;

void* StringValSymbolMapLock;

void StringSymbolFuncAdd(std::wstring name, SystemFuncDef func, uint16_t argumentCount) {

	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (fnref.content.function) ScriptFunction(ScriptFunction::System);
	stringman_script_function_alloc.push_back(fnref.content.function);
	fnref.content.function->argumentCount = argumentCount;
	fnref.content.function->funcImpl.system_func = func;
	string_symbol_map[name] = fnref;

}

void StringManager_Init()
{
	StringValSymbolMapLock = platform.MutexCreate();

	StringSymbolFuncAdd(L"charAt", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
		if (args[0].getRawVariable()->varType != ValueType::NUM) {
			currentWorker->ThrowError(L"charAt(index) only accept number");
			return VariableValue();
		 }
		uint32_t index = (uint32_t)args[0].content.number;
		std::wstring& impl = thisValue->implement.stringImpl;
		return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(&impl[index], 1)));
	}, 1);

    

    // charCodeAt
    StringSymbolFuncAdd(L"charCodeAt", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() > 0 && args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError(L"charCodeAt(index) only accept number");
            return VariableValue();
        }
        uint32_t index = args.size() > 0 ? (uint32_t)args[0].content.number : 0;
        std::wstring& impl = thisValue->implement.stringImpl;
        if (index >= impl.length()) {
            return CreateNumberVariable(std::numeric_limits<double>::quiet_NaN());
        }
        return CreateNumberVariable((double)impl[index]);
        }, 1);

    StringSymbolFuncAdd(L"split", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring& str = thisValue->implement.stringImpl;

        // 处理参数
        std::wstring separator = L","; // 默认分隔符为逗号
        int limit = -1; // 默认无限制

        if (args.size() >= 1) {
            if (args[0].getContentType() != ValueType::STRING) {
                currentWorker->ThrowError(L"split(separator, limit) first argument must be string");
                return VariableValue();
            }
            separator = args[0].getRawVariable()->content.ref->implement.stringImpl;
        }

        if (args.size() >= 2) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError(L"split(separator, limit) second argument must be number");
                return VariableValue();
            }
            limit = (int)args[1].content.number;
            if (limit < 0) limit = -1;
        }

        VMObject* arrayObj = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        auto& resultArray = arrayObj->implement.arrayImpl;

        // 空字符串特殊情况
        if (str.empty()) {
            if (limit != 0) {
                VMObject* emptyStrObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(L""));
                resultArray.push_back(CreateReferenceVariable(emptyStrObj));
            }
            return CreateReferenceVariable(arrayObj);
        }

        // 分隔符为空字符串，按字符分割
        if (separator.empty()) {
            int count = 0;
            for (wchar_t ch : str) {
                if (limit != -1 && count >= limit) break;
                VMObject* charObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(1, ch));
                resultArray.push_back(CreateReferenceVariable(charObj));
                count++;
            }
            return CreateReferenceVariable(arrayObj);
        }

        // 正常分割逻辑
        size_t start = 0;
        size_t end = str.find(separator);
        int splitCount = 0;
        bool limitReached = false;

        while (end != std::wstring::npos && !limitReached) {
            // 检查是否达到限制
            if (limit != -1 && splitCount >= limit - 1) {
                limitReached = true;
                break;
            }

            VMObject* partObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(str.substr(start, end - start));
            resultArray.push_back(CreateReferenceVariable(partObj));

            start = end + separator.length();
            end = str.find(separator, start);
            splitCount++;
        }

        // 添加最后一段（或剩余部分）
        if (!limitReached || (limit != -1 && splitCount < limit)) {
            VMObject* lastPartObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(str.substr(start));
            resultArray.push_back(CreateReferenceVariable(lastPartObj));
        }

        // 如果limit=0，返回空数组
        if (limit == 0) {
            resultArray.clear();
        }

        return CreateReferenceVariable(arrayObj);
        }, DYNAMIC_ARGUMENT);

    // concat
    StringSymbolFuncAdd(L"concat", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring result = thisValue->implement.stringImpl;
        for (auto& arg : args) {
            result += arg.ToString();
        }
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
        }, DYNAMIC_ARGUMENT);

    // indexOf
    StringSymbolFuncAdd(L"indexOf", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"indexOf(searchString) requires a string argument");
            return VariableValue();
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring& searchStr = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t fromIndex = 0;

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            fromIndex = (size_t)args[1].content.number;
        }

        if (fromIndex >= str.length()) {
            return CreateNumberVariable(-1.0);
        }

        size_t pos = str.find(searchStr, fromIndex);
        return CreateNumberVariable(pos == std::wstring::npos ? -1.0 : (double)pos);
        }, 1);

    // lastIndexOf
    StringSymbolFuncAdd(L"lastIndexOf", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"lastIndexOf(searchString) requires a string argument");
            return VariableValue();
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring& searchStr = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t fromIndex = str.length() - 1;

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            fromIndex = (size_t)args[1].content.number;
        }

        size_t pos = str.rfind(searchStr, std::min(fromIndex, str.length() - 1));
        return CreateNumberVariable(pos == std::wstring::npos ? -1.0 : (double)pos);
        }, 1);

    // substring
    StringSymbolFuncAdd(L"substring", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring& str = thisValue->implement.stringImpl;

        if (args.size() == 0) {
            currentWorker->ThrowError(L"substring requires at least 1 argument");
            return VariableValue();
        }

        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError(L"substring indices must be numbers");
            return VariableValue();
        }

        int start = (int)args[0].content.number;
        int end = (int)str.length();

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            end = (int)args[1].content.number;
        }

        // 处理边界情况
        if (start < 0) start = 0;
        if (end < 0) end = 0;
        if (start > end) std::swap(start, end);
        if (start > (int)str.length()) start = (int)str.length();
        if (end > (int)str.length()) end = (int)str.length();

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(start, end - start)
        ));
        }, DYNAMIC_ARGUMENT);

    // toLowerCase
    StringSymbolFuncAdd(L"toLowerCase", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring str = thisValue->implement.stringImpl;
        std::transform(str.begin(), str.end(), str.begin(), ::towlower);
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(str));
        }, 0);

    // toUpperCase
    StringSymbolFuncAdd(L"toUpperCase", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring str = thisValue->implement.stringImpl;
        std::transform(str.begin(), str.end(), str.begin(), ::towupper);
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(str));
        }, 0);

    // trim
    StringSymbolFuncAdd(L"trim", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring str = thisValue->implement.stringImpl;

        // 去除前导空白
        size_t start = str.find_first_not_of(L" \t\n\r\f\v");
        if (start == std::wstring::npos) {
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(L"")));
        }

        // 去除尾部空白
        size_t end = str.find_last_not_of(L" \t\n\r\f\v");

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(start, end - start + 1)
        ));
        }, 0);

    // startsWith
    StringSymbolFuncAdd(L"startsWith", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"startsWith requires a string argument");
            return VariableValue();
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring& searchStr = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t pos = 0;

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            pos = (size_t)args[1].content.number;
        }

        if (pos >= str.length()) {
            return CreateBooleanVariable(false);
        }

        bool result = (str.compare(pos, searchStr.length(), searchStr) == 0);
        return CreateBooleanVariable(result);
        }, 1);

    // endsWith
    StringSymbolFuncAdd(L"endsWith", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"endsWith requires a string argument");
            return VariableValue();
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring& searchStr = args[0].getRawVariable()->content.ref->implement.stringImpl;

        if (searchStr.length() > str.length()) {
            return CreateBooleanVariable(false);
        }

        size_t pos = str.length() - searchStr.length();

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            pos = (size_t)args[1].content.number;
            if (pos + searchStr.length() > str.length()) {
                pos = str.length() - searchStr.length();
            }
        }

        bool result = (str.compare(pos, searchStr.length(), searchStr) == 0);
        return CreateBooleanVariable(result);
        }, 1);

    // includes
    StringSymbolFuncAdd(L"includes", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"includes requires a string argument");
            return VariableValue();
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring& searchStr = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t pos = 0;

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            pos = (size_t)args[1].content.number;
        }

        bool result = (str.find(searchStr, pos) != std::wstring::npos);
        return CreateBooleanVariable(result);
        }, 1);

    // repeat
    StringSymbolFuncAdd(L"repeat", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError(L"repeat requires a number argument");
            return VariableValue();
        }

        int count = (int)args[0].content.number;
        if (count < 0) {
            currentWorker->ThrowError(L"repeat count must be non-negative");
            return VariableValue();
        }

        if (count == 0) {
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(L"")));
        }

        std::wstring& str = thisValue->implement.stringImpl;
        std::wstring result;

        for (int i = 0; i < count; i++) {
            result += str;
        }

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
        }, 1);

    // slice
    StringSymbolFuncAdd(L"slice", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::wstring& str = thisValue->implement.stringImpl;

        if (args.size() == 0) {
            currentWorker->ThrowError(L"slice requires at least 1 argument");
            return VariableValue();
        }

        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError(L"slice indices must be numbers");
            return VariableValue();
        }

        int start = (int)args[0].content.number;
        int end = (int)str.length();

        if (args.size() > 1 && args[1].getRawVariable()->varType == ValueType::NUM) {
            end = (int)args[1].content.number;
        }

        // 处理负数索引
        if (start < 0) start = std::max(0, (int)str.length() + start);
        if (end < 0) end = std::max(0, (int)str.length() + end);

        start = std::min(start, (int)str.length());
        end = std::min(end, (int)str.length());

        if (start >= end) {
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(std::wstring(L"")));
        }

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(start, end - start)
        ));
        }, DYNAMIC_ARGUMENT);
}

void StringManager_Destroy()
{
	platform.MutexDestroy(StringValSymbolMapLock);

	for (auto& pair : string_symbol_map) {
		if (pair.second.varType == ValueType::FUNCTION) {
			pair.second.content.function->~ScriptFunction();
			platform.MemoryFree(pair.second.content.function);
		}
	}

	string_symbol_map.clear();
}

VariableValue GetStringValSymbol(std::wstring& symbol, VMObject* owner)
{
	//对动态字段的特殊处理
	if (symbol == L"length") {
		VariableValue ret;
		ret.varType = ValueType::NUM;
		ret.content.number = owner->implement.stringImpl.length();
		return ret;
	}

	platform.MutexLock(StringValSymbolMapLock);

	if (string_symbol_map.find(symbol) == string_symbol_map.end()) {
		platform.MutexUnlock(StringValSymbolMapLock);
		return VariableValue(); //如果不存在就返回NULLREF，避免破坏
	}

	//复制并设置thisValue
	VariableValue ret = string_symbol_map[symbol];
	ret.thisValue = owner;
	platform.MutexUnlock(StringValSymbolMapLock);
	return ret;
}
