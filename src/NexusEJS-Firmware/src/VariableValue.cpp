#include "VariableValue.h"
#include <vector>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include "VM.h"

constexpr uint16_t ToStringDepth = 10; //VariableValue::ToString最大递归深度

bool isRefType(VariableValue* vrb) {
    return vrb->getRawVariable()->varType == ValueType::REF;
}


VariableValue* VariableValue::getRawVariable() {
    VariableValue* current = this;
    while (current->varType == ValueType::BRIDGE) {
        current = current->content.bridge_ref;
    }
    return current;
}


const VariableValue* VariableValue::getRawVariableConst() const
{
    const VariableValue* current = this;
    while (current->varType == ValueType::BRIDGE) {
        current = current->content.bridge_ref;
    }
    return current;
}


ValueType::IValueType VariableValue::getContentType()
{
    const VariableValue* current = this->getRawVariable();
    if (current->varType == ValueType::REF) {
        return current->content.ref->type;
    }
    else {
        return current->varType;
    }
}

std::string nexus_d2str(double d, int prec = 6) {
    std::ostringstream wss;
    wss << std::fixed << std::setprecision(prec) << d;
    std::string s = wss.str();

    s.erase(s.find_last_not_of('0') + 1);
    if (s.back() == '.') s.pop_back();

    return s;
}

std::string VariableValue::ToString(uint16_t depth) const
{
    if (depth > ToStringDepth) {
        return "<max_depth>";
    }

    const VariableValue* raw = getRawVariableConst();
    if(raw->varType == ValueType::NUM) {
        return nexus_d2str(raw->content.number);
    }
    else if(raw->varType == ValueType::BOOL) {
        return raw->content.boolean ? "true" : "false";
    }
    else if(raw->varType == ValueType::PTR) {
        return std::to_string(raw->content.ptr);
    }
    else if (raw->varType == ValueType::REF) {
        return raw->content.ref->ToString(depth); //depth直接传入，因为引用不增加深度
    }
    else if (raw->varType == ValueType::FUNCTION) {
        return "<func>";
    }
    else if (raw->varType == ValueType::NULLREF) {
        return "null";
    }
    return "no-support-var-str";
}

static void __objectStringHelper(std::unordered_map<std::string,VariableValue>* val,std::ostringstream& stream,uint16_t depth) {
    stream << "{";
    int index = 0;
    for (auto& pair : *val) {
        if (index != 0)stream << ',';

        stream << '\"' << pair.first << "\":";
        
        VariableValue* value = pair.second.getRawVariable();
        if (value->varType == ValueType::REF &&
            value->content.ref->type == ValueType::STRING) {
            //字符串加上引号
            stream << '\"' << value->ToString(depth + 1) << '\"';
        }
        else {
            stream << value->ToString(depth + 1);
        }

        index++;
    }

    stream << '}';
}

std::string VMObject::ToString(uint16_t depth) {
    switch (type)
    {
    case ValueType::NULLREF:
        return "null";
        break;
    case ValueType::CONTEXT:
        break;
    case ValueType::ANY:
        break;
    case ValueType::NUM: //值类型不会出现在这里
        break;
    case ValueType::STRING:
    {
        //std::string* str = std::get_if<std::string>(&implement);
        return implement.stringImpl;
    }
    case ValueType::BOOL: //值类型不会出现在这里
        break;
    case ValueType::ARRAY:
    {
        //std::vector<VariableValue>* arr = std::get_if<std::vector<VariableValue>>(&implement);
        std::vector<VariableValue>* arr = &implement.arrayImpl;

        std::ostringstream stream;
        stream << "[";
        for (int i = 0; i < arr->size(); i++) {
            if (i != 0) stream << ",";
            VariableValue& value = arr->at(i);
            if (value.varType == ValueType::REF &&
                value.content.ref->type == ValueType::STRING) {
                //字符串加上引号
                stream << '\"' << value.ToString(depth + 1) << '\"';
            }
            else {
                stream << value.ToString(depth + 1);
            }
        }
        stream << "]";
        return stream.str();

        break;
    }
    case ValueType::OBJECT:
    {
        std::unordered_map<std::string, VariableValue>* objContainer = &implement.objectImpl;

        std::ostringstream stream;
        __objectStringHelper(objContainer, stream,depth); //depth在辅助函数内实现自增

        return stream.str();

        break;
    }
    case ValueType::FUNCTION:
    {
        auto ret = "<clos_fn>";
        if (this->implement.closFuncImpl.propObject) {
            return ret + this->implement.closFuncImpl.propObject->ToString(depth);
        }
        return ret;
    }
    case ValueType::PTR: //值类型不会出现在这里
        break;
    case ValueType::PROMISE:
        break;
    default:
        return "error-object";
    }
    return "unknown-object";
}

//this一定要是引用类型所以不需要BRIDGE类型
VariableValue ScriptFunction::InvokeFunc(std::vector<VariableValue>& args, VMObject* thisValue, VMObject* closure, VMWorker* currentWorker)
{
    
    switch (this->type)
    {
    case Local: 
    {
        auto& funcInfo = this->funcImpl.local_func;
        FuncFrame frame;
        frame.byteCode = funcInfo.byteCode;
        frame.byteCodeLength = funcInfo.byteCodeLength;
        frame.functionInfo = &funcInfo;
        //frame.thisValue = thisValue;
        ScopeFrame defaultScope;
        //初始化默认作用域
        defaultScope.byteCodeLength = funcInfo.byteCodeLength;
        defaultScope.byteCodeStart = 0;
        defaultScope.ep = 0;
        defaultScope.spStart = 0;
        //向新的栈帧的第一个作用域帧填充参数
        uint32_t index = 0;
        for (auto& arg : args) {
            frame.localVariables.push_back(arg);
            frame.localVarNames.push_back(funcInfo.arguments[index]);
            index++;
        }

        VariableValue thisValueRef;
        if (thisValue) { //如果thisValue是NULL就转换一下，避免REF类型访问空指针
            thisValueRef.varType = ValueType::REF;
            thisValueRef.content.ref = thisValue;
        }
        else {
            thisValueRef.varType = ValueType::NULLREF;
        }
        frame.functionEnvSymbols["this"] = thisValueRef;

        if (closure) {
            frame.functionEnvSymbols["_clos"] = CreateReferenceVariable(closure);
        }

        frame.scopeStack.push_back(defaultScope);
        currentWorker->getCallingLink().push_back(frame);

        //移交控制权给虚拟机继续执行脚本函数
        VariableValue result; 
        result.varType = ValueType::NULLREF;//需要等待栈帧返回，此时返回NULLREF
        return result;
    }
    case Native: 
    {
        //原生方法阻塞到结束
        //todo 未实现
    }
    case System:
    {
        //系统原生方法阻塞到结束并且返回值
        return this->funcImpl.system_func(args, thisValue,currentWorker);
    }
    default:
        break; 
    }

    return VariableValue();
}
