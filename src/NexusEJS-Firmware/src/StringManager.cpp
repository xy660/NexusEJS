#include "StringManager.h"
#include "PlatformImpl.h"
#include <stdint.h>
#include <algorithm>
#include "VM.h"
#include "ObjectManager.h"

//ARRAY对象符号表
//返回方法包装的VMObject对象，或

VMObject stringPrototype(ValueType::OBJECT);

std::unordered_map<std::string, VariableValue>& string_symbol_map = stringPrototype.implement.objectImpl;
std::vector<ScriptFunction*> stringman_script_function_alloc;

void* StringValSymbolMapLock;

void StringSymbolFuncAdd(std::string name, SystemFuncDef func, uint16_t argumentCount) {

	VariableValue fnref;
	fnref.varType = ValueType::FUNCTION;
	fnref.content.function = (ScriptFunction*)platform.MemoryAlloc(sizeof(ScriptFunction));
	new (fnref.content.function) ScriptFunction(ScriptFunction::System);
	stringman_script_function_alloc.push_back(fnref.content.function);
	fnref.content.function->argumentCount = argumentCount;
	fnref.content.function->funcImpl.system_func = func;
	string_symbol_map[name] = fnref;

}

// 辅助函数
static size_t getUtf8CharPos(const std::string& str, int charIndex) {
    int charCount = 0;
    for (size_t i = 0; i < str.length();) {
        if (charCount == charIndex) return i;
        unsigned char c = (unsigned char)str[i];
        if (c >= 0xF0) i += 4;
        else if (c >= 0xE0) i += 3;
        else if (c >= 0xC0) i += 2;
        else i++;
        charCount++;
    }
    return str.length();
}

static int getUtf8CharCount(const std::string& str) {
    int charCount = 0;
    for (size_t i = 0; i < str.length();) {
        unsigned char c = (unsigned char)str[i];
        if (c >= 0xF0) i += 4;
        else if (c >= 0xE0) i += 3;
        else if (c >= 0xC0) i += 2;
        else i++;
        charCount++;
    }
    return charCount;
}

static int getUtf8CharLength(unsigned char c) {
    if (c < 0x80) return 1;
    if (c < 0xE0) return 2;
    if (c < 0xF0) return 3;
    return 4;
}

void StringManager_Init()
{
    StringValSymbolMapLock = platform.MutexCreate();

    StringSymbolFuncAdd("charAt", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError("charAt(index) only accept number");
            return VariableValue();
        }
        int index = (int)args[0].content.number;
        std::string& str = thisValue->implement.stringImpl;

        if (index < 0 || index >= str.length()) {
            // JavaScript中，charAt(index)当索引越界时返回空字符串
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(""));
        }

        int charCount = 0;
        for (size_t i = 0; i < str.length();) {
            if (charCount == index) {
                unsigned char c = (unsigned char)str[i];
                int charLen = getUtf8CharLength(c);
                if (i + charLen > str.length()) charLen = 1;
                return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
                    str.substr(i, charLen)
                ));
            }

            unsigned char c = (unsigned char)str[i];
            i += getUtf8CharLength(c);
            charCount++;
        }

        // 如果索引超出字符数，也返回空字符串
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(""));
        }, 1);

    StringSymbolFuncAdd("charCodeAt", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        int index = args.size() > 0 ? (int)args[0].content.number : 0;
        std::string& str = thisValue->implement.stringImpl;

        if (index < 0) {
            return CreateNumberVariable(std::numeric_limits<double>::quiet_NaN());
        }

        int charCount = 0;
        for (size_t i = 0; i < str.length();) {
            if (charCount == index) {
                unsigned char c = (unsigned char)str[i];
                int codePoint = 0;

                if (c < 0x80) {
                    codePoint = c;
                }
                else if (c < 0xE0 && i + 1 < str.length()) {
                    codePoint = ((c & 0x1F) << 6) | (str[i + 1] & 0x3F);
                }
                else if (c < 0xF0 && i + 2 < str.length()) {
                    codePoint = ((c & 0x0F) << 12) | ((str[i + 1] & 0x3F) << 6) | (str[i + 2] & 0x3F);
                }
                else if (i + 3 < str.length()) {
                    codePoint = ((c & 0x07) << 18) | ((str[i + 1] & 0x3F) << 12) |
                        ((str[i + 2] & 0x3F) << 6) | (str[i + 3] & 0x3F);
                }
                else {
                    codePoint = c;
                }

                return CreateNumberVariable((double)codePoint);
            }

            unsigned char c = (unsigned char)str[i];
            i += getUtf8CharLength(c);
            charCount++;
        }

        return CreateNumberVariable(std::numeric_limits<double>::quiet_NaN());
        }, 1);

    StringSymbolFuncAdd("split", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::string& str = thisValue->implement.stringImpl;

        std::string separator = ",";
        int limit = -1;

        if (args.size() >= 1) {
            if (args[0].getContentType() != ValueType::STRING) {
                currentWorker->ThrowError("split(separator, limit) first argument must be string");
                return VariableValue();
            }
            separator = args[0].getRawVariable()->content.ref->implement.stringImpl;
        }

        if (args.size() >= 2) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("split(separator, limit) second argument must be number");
                return VariableValue();
            }
            limit = (int)args[1].content.number;
        }

        VMObject* arrayObj = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        auto& resultArray = arrayObj->implement.arrayImpl;

        if (separator.empty()) {
            int count = 0;
            for (size_t i = 0; i < str.length();) {
                if (limit != -1 && count >= limit) break;

                unsigned char c = (unsigned char)str[i];
                int charLen = getUtf8CharLength(c);

                VMObject* charObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(str.substr(i, charLen));
                resultArray.push_back(CreateReferenceVariable(charObj));

                i += charLen;
                count++;
            }
            return CreateReferenceVariable(arrayObj);
        }

        size_t start = 0;
        size_t end = str.find(separator);
        int splitCount = 0;

        while (end != std::string::npos && (limit == -1 || splitCount < limit - 1)) {
            resultArray.push_back(CreateReferenceVariable(
                currentWorker->VMInstance->currentGC->GC_NewStringObject(str.substr(start, end - start))
            ));
            start = end + separator.length();
            end = str.find(separator, start);
            splitCount++;
        }

        if (limit != 0) {
            resultArray.push_back(CreateReferenceVariable(
                currentWorker->VMInstance->currentGC->GC_NewStringObject(str.substr(start))
            ));
        }

        return CreateReferenceVariable(arrayObj);
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("concat", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::string result = thisValue->implement.stringImpl;
        for (auto& arg : args) {
            result += arg.ToString();
        }
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("indexOf", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("indexOf requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError("indexOf requires a string argument");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string& search = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t from = 0;

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("indexOf second argument must be a number");
                return VariableValue();
            }
            from = (size_t)args[1].content.number;
        }

        size_t pos = str.find(search, from);
        return CreateNumberVariable(pos == std::string::npos ? -1.0 : (double)pos);
        }, DYNAMIC_ARGUMENT);

    // lastIndexOf - 动态参数，手动检查
    StringSymbolFuncAdd("lastIndexOf", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("lastIndexOf requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError("lastIndexOf requires a string argument");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string& search = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t from = str.length() - 1;

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("lastIndexOf second argument must be a number");
                return VariableValue();
            }
            from = (size_t)args[1].content.number;
        }

        size_t pos = str.rfind(search, std::min(from, str.length() - 1));
        return CreateNumberVariable(pos == std::string::npos ? -1.0 : (double)pos);
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("substring", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("substring requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError("substring indices must be numbers");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        int startChar = (int)args[0].content.number;
        int endChar = getUtf8CharCount(str);

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("substring indices must be numbers");
                return VariableValue();
            }
            endChar = (int)args[1].content.number;
        }

        if (startChar < 0) startChar = 0;
        if (endChar < 0) endChar = 0;
        if (startChar > endChar) std::swap(startChar, endChar);

        int totalChars = getUtf8CharCount(str);
        if (startChar > totalChars) startChar = totalChars;
        if (endChar > totalChars) endChar = totalChars;

        size_t startByte = getUtf8CharPos(str, startChar);
        size_t endByte = getUtf8CharPos(str, endChar);

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(startByte, endByte - startByte)
        ));
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("toLowerCase", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::string str = thisValue->implement.stringImpl;
        std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(str));
        }, 0);

    StringSymbolFuncAdd("toUpperCase", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::string str = thisValue->implement.stringImpl;
        std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(str));
        }, 0);

    StringSymbolFuncAdd("trim", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        std::string str = thisValue->implement.stringImpl;

        size_t start = str.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) {
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(""));
        }

        size_t end = str.find_last_not_of(" \t\n\r\f\v");
        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(start, end - start + 1)
        ));
        }, 0);

    StringSymbolFuncAdd("startsWith", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("startsWith requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError("startsWith requires a string argument");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string& search = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t pos = 0;

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("startsWith second argument must be a number");
                return VariableValue();
            }
            pos = (size_t)args[1].content.number;
        }

        if (pos >= str.length()) {
            return CreateBooleanVariable(false);
        }

        bool result = (str.compare(pos, search.length(), search) == 0);
        return CreateBooleanVariable(result);
        }, DYNAMIC_ARGUMENT);

    // endsWith - 动态参数，手动检查
    StringSymbolFuncAdd("endsWith", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("endsWith requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError("endsWith requires a string argument");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string& search = args[0].getRawVariable()->content.ref->implement.stringImpl;

        if (search.length() > str.length()) {
            return CreateBooleanVariable(false);
        }

        size_t pos = str.length() - search.length();

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("endsWith second argument must be a number");
                return VariableValue();
            }
            pos = (size_t)args[1].content.number;
            if (pos + search.length() > str.length()) {
                pos = str.length() - search.length();
            }
        }

        bool result = (str.compare(pos, search.length(), search) == 0);
        return CreateBooleanVariable(result);
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("slice", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("slice requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getRawVariable()->varType != ValueType::NUM) {
            currentWorker->ThrowError("slice indices must be numbers");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        int totalChars = getUtf8CharCount(str);
        int startChar = (int)args[0].content.number;
        int endChar = totalChars;

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("slice indices must be numbers");
                return VariableValue();
            }
            endChar = (int)args[1].content.number;
        }

        if (startChar < 0) startChar = totalChars + startChar;
        if (endChar < 0) endChar = totalChars + endChar;
        if (startChar < 0) startChar = 0;
        if (endChar < 0) endChar = 0;
        if (startChar > totalChars) startChar = totalChars;
        if (endChar > totalChars) endChar = totalChars;

        if (startChar >= endChar) {
            return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(""));
        }

        size_t startByte = getUtf8CharPos(str, startChar);
        size_t endByte = getUtf8CharPos(str, endChar);

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(
            str.substr(startByte, endByte - startByte)
        ));
        }, DYNAMIC_ARGUMENT);

    StringSymbolFuncAdd("repeat", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        int count = (int)args[0].content.number;
        if (count < 0) {
            currentWorker->ThrowError("repeat count must be non-negative");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string result;

        for (int i = 0; i < count; i++) {
            result += str;
        }

        return CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(result));
        }, 1);

    StringSymbolFuncAdd("includes", [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.size() < 1) {
            currentWorker->ThrowError("includes requires at least 1 argument");
            return VariableValue();
        }
        if (args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError("includes requires a string argument");
            return VariableValue();
        }

        std::string& str = thisValue->implement.stringImpl;
        std::string& search = args[0].getRawVariable()->content.ref->implement.stringImpl;
        size_t pos = 0;

        if (args.size() > 1) {
            if (args[1].getRawVariable()->varType != ValueType::NUM) {
                currentWorker->ThrowError("includes second argument must be a number");
                return VariableValue();
            }
            pos = (size_t)args[1].content.number;
        }

        bool result = (str.find(search, pos) != std::string::npos);
        return CreateBooleanVariable(result);
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

VariableValue GetStringValSymbol(std::string& symbol, VMObject* owner)
{
    // 对动态字段的特殊处理
    if (symbol == "length") {
        VariableValue ret;
        ret.varType = ValueType::NUM;

        std::string& str = owner->implement.stringImpl;

        // 模拟JavaScript的UTF-16字符计数
        int jsLength = 0;
        for (size_t i = 0; i < str.length(); ) {
            unsigned char c = str[i];

            if (c < 0x80) {
                // 单字节ASCII
                jsLength++;
                i++;
            }
            else if (c < 0xC0) {
                // 继续字节
                i++;
            }
            else if (c < 0xE0) {
                // 2字节UTF-8
                jsLength++;
                i += 2;
            }
            else if (c < 0xF0) {
                // 3字节UTF-8
                jsLength++;
                i += 3;
            }
            else {
                //4字节UTF-8代理对
                jsLength += 2;
                i += 4;
            }
        }

        ret.content.number = jsLength;
        return ret;
    }

    //直接查找原型链拷贝返回（不允许赋值修改，赋值无效）
    VariableValue ret;
    GetObjectField(symbol, &stringPrototype, ret, false);
    
    ret = *ret.getRawVariable(); //解引用，直接返回值，阻止赋值修改

    ret.thisValue = owner;

	return ret;
}
