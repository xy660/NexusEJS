#pragma once

#ifndef VARIABLE_VALUE_H
#define VARIABLE_VALUE_H

#pragma message("VariableValue.h被包含！") 

#endif

#include<unordered_map>
#include<vector>
#include<stdint.h>
#include<string>
#include <cstring>
#include <variant>
#include <sstream>
#include "PlatformImpl.h"
#include "ByteCode.h"

class VMObject;
class VariableValue;
class ScriptFunction;
class VariableValue;
class VM;

class VMWorker;

typedef VariableValue (*SystemFuncDef)(std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker);

inline bool operator ==(const VMObject& a, const VMObject& b);
inline bool operator ==(const VariableValue& left, const VariableValue& right);
inline bool operator !=(const VariableValue& a, const VariableValue& b);


class ValueType {
public:
    enum IValueType : uint8_t
    {
        UNDEFINED, //未定义
        CONTEXT,
        ANY,
        NUM,
        STRING,
        BOOL,
        ARRAY,
        OBJECT,
        FUNCTION,
        PTR,
        PROMISE,
        BRIDGE, //VariableValue指针代理
        REF, //VariableValue引用类型
        NULLREF, //空引用 
    };
};

bool isRefType(VariableValue* vrb);

struct ClosureFunctionImpl {
    ScriptFunction* sfn;
    VMObject* closure;
    VMObject* propObject; //存储函数对象的成员（内联的对象）*目前未实现，此为保留字段*
};

//变量引用，不存储实际对象值
//随栈释放
class VariableValue {
public:

    union Content
    {
        VMObject* ref; //对象引用
        ScriptFunction* function; //脚本函数存储
        VariableValue* bridge_ref; //存储代理桥接类型，例如GET_FIELD LOAD_VAR返回的就是这种
        double number; //值类型数字
        uintptr_t ptr; //值类型指针
        bool boolean; //值类型布尔
    } content;
    ValueType::IValueType varType;

    VMObject* thisValue; //脚本函数动态绑定的this，由GET_FILED指令动态设置

    bool readOnly = false;

    //获取真实类型，每次使用值都需要调用
    VariableValue* getRawVariable();
    const VariableValue* getRawVariableConst() const;

    ValueType::IValueType getContentType();

    std::string ToString(uint16_t depth = 0) const;

    bool Truty();

    VariableValue() {
        content = { 0 };
        varType = ValueType::NULLREF;
        thisValue = NULL;
    }
};


class VMObject {
public:
    
    bool marked; //GC要用

    enum VMObjectProtectStatus {
        NONE,
        PROTECTED,
        NOT_PROTECTED,
    } protectStatus;
    
    //bool flag_isLocalObject : 1;
    ValueType::IValueType type = ValueType::NULLREF; //存储类型

    void* mutex; //对象锁，按需分配，默认null


    union VMOImplement
    {
        std::unordered_map<std::string, VariableValue> objectImpl;
        std::vector<VariableValue> arrayImpl;
        std::string stringImpl;
        ClosureFunctionImpl closFuncImpl;
        VMOImplement(){
        }
        ~VMOImplement() {};
    } implement;


    std::string ToString(uint16_t depth = 0);


    //复制语义，未来需要再启用
    /*

    void CopyObject(const VMObject& v) {
        this->flag_isLocalObject = v.flag_isLocalObject;
        this->marked = v.marked;
        this->mutex = v.mutex;
        this->type = v.type;
        switch (this->type)
        {
        case ValueType::STRING:
        {
            this->implement.stringImpl = v.implement.stringImpl;
            break;
        }
        case ValueType::OBJECT:
        {
            this->implement.objectImpl = v.implement.objectImpl;
            break;
        }
        case ValueType::ARRAY:
        {
            this->implement.arrayImpl = v.implement.arrayImpl;
        }
        }
    }

    VMObject& operator=(const VMObject& other) {
        switch (this->type)
        {
        case ValueType::STRING: implement.stringImpl.~basic_string(); break;
        case ValueType::ARRAY: implement.arrayImpl.~vector(); break;
        case ValueType::OBJECT: implement.objectImpl.~unordered_map(); break;
        }
        if (mutex) {
            platform.MutexDestroy(mutex);
        }
        CopyObject(other);
    }

    VMObject(const VMObject& v) {
        CopyObject(v);
    }

    */

    VMObject(ValueType::IValueType type) {
        //this->flag_isLocalObject = false;
        this->type = type;
        this->marked = false;
        this->protectStatus = NONE;
        this->mutex = NULL;
        switch (type)
        {
        case ValueType::STRING:
        {
            new (&implement.stringImpl) std::string();
            break;
        }
        case ValueType::ARRAY:
        {
            new (&implement.arrayImpl) std::vector<VariableValue>();
            break;
        }
        case ValueType::OBJECT:
        {
            new (&implement.objectImpl) std::unordered_map<std::string, VariableValue>();
            break;
        }
        case ValueType::FUNCTION:
        {
            //直接初始化内存块
            memset(&implement.closFuncImpl, 0, sizeof(ClosureFunctionImpl));
            break;
        }
        default:
            break;
        }
    }

    ~VMObject() {
        switch (this->type)
        {
        case ValueType::STRING:
        {
            implement.stringImpl.~basic_string();
            break;
        }
        case ValueType::ARRAY:
        {
            implement.arrayImpl.~vector();
            break;
        }
        case ValueType::OBJECT:
        {
            implement.objectImpl.~unordered_map();
            break;
        }
        case ValueType::FUNCTION:
            break; 
        default:
            break;
        }
        //如果mutex存在销毁mutex
        if (mutex) {
            platform.MutexDestroy(mutex);
        }
    }



};


class ScriptFunction {
public:
    enum FuncType : uint8_t {
        Local,
        Native, //暂时没有这个，因为FFI未实现
        System
    } type;

    //参数数量
    uint8_t argumentCount = 0; 

    union FuncImpl {
        SystemFuncDef system_func;
        ByteCodeFunction local_func;
        FuncImpl(){
            //外部初始化
        }
        ~FuncImpl() {
        }
    }funcImpl;

    //Native&System类型方法返回VariableValue，Local方法需要等待栈帧返回填充，此方法返回NULLREF
    VariableValue InvokeFunc(std::vector<VariableValue>& args,VMObject* thisValue,VMObject* closure,VMWorker* currentWorker);

    ScriptFunction(FuncType fnType) {
        type = fnType;
        if (fnType == Local) {
            new (&funcImpl.local_func) ByteCodeFunction();
        }
        else if (fnType == System) {
            funcImpl.system_func = NULL;
        }
    }

    ~ScriptFunction() {
        // 手动析构正确的成员
        if (type == Local) {
            funcImpl.local_func.~ByteCodeFunction();
        }
        else if(type == System) {
            //指针不需要释放
        }
    }
};

class PackageContext {
public:
    uint16_t packageId;
    uint8_t GCMarked = false;
    std::vector<VMObject*> ConstStringPool;
    std::unordered_map<std::string, VariableValue> bytecodeFunctions;
    std::string packageName;
};


inline bool operator ==(const VariableValue& left, const VariableValue& right) {
    const VariableValue* a = left.getRawVariableConst();
    const VariableValue* b = right.getRawVariableConst();
    //获取一下最终指向的目标，避免误把代理类型当作最终类型
    if (a->varType != b->varType) {
        return false;
    }
    if (a->varType == ValueType::NUM) {
        return a->content.number == b->content.number;
    }
    else if (a->varType == ValueType::BOOL) {
        return a->content.boolean == b->content.boolean;
    }
    else if (a->varType == ValueType::PTR) {
        return a->content.ptr == b->content.ptr;
    }
    else if (a->varType == ValueType::REF) {

        if (a->content.ref && b->content.ref) {
            return *a->content.ref == *b->content.ref;
        }
        return false;
    }
    else if (a->varType == ValueType::UNDEFINED || a->varType == ValueType::NULLREF) {
        return b->varType == ValueType::UNDEFINED || b->varType == ValueType::NULLREF;
    }
    return false;
}

inline bool operator !=(const VariableValue& a, const VariableValue& b) {
    return !(a == b);
}

inline bool operator ==(const VMObject& a, const VMObject& b) {
    if (a.type != b.type) {
        return false;
    }
    //return a.implement == b.implement;
    //VMObject作为引用类型只能存在这几种类型
    if (a.type == ValueType::ARRAY) {
        return a.implement.arrayImpl == b.implement.arrayImpl;
    }
    else if (a.type == ValueType::OBJECT) {
        return a.implement.objectImpl == b.implement.objectImpl;
    }
    else if (a.type == ValueType::STRING) {
        return a.implement.stringImpl == b.implement.stringImpl;
    }
    return false;
}

inline VariableValue CreateNumberVariable(double value) { 
    VariableValue ret;
    ret.varType = ValueType::NUM;
    ret.content.number = value;
    return ret;
}

inline VariableValue CreateBooleanVariable(bool value) {
    VariableValue ret;
    ret.varType = ValueType::BOOL;
    ret.content.boolean = value;
    return ret;
}
inline VariableValue CreateReferenceVariable(VMObject* value,bool readOnly = false) {
    VariableValue ret;
    ret.varType = ValueType::REF;
    ret.content.ref = value;
    ret.readOnly = readOnly;
    return ret;
}