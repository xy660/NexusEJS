#pragma once

#include <stdint.h>
#include <string>
#include <vector>
#include <unordered_map>

#include "PlatformImpl.h"



class ByteCodeFunction
{
public:
    uint8_t* byteCode; 
	uint32_t byteCodeLength; //字节码长度
    std::vector<uint16_t> arguments;
    std::vector<uint16_t> outsideSymbols; //存储外部符号依赖
    uint32_t packageId; 
    std::string funcName;
    uint16_t funcNameStrId;
    ByteCodeFunction() {
        byteCode = NULL;
        byteCodeLength = 0;
        packageId = 0xFFFFFFFF;
        funcNameStrId = 0xFFFFFFFF;
    }

    ByteCodeFunction(const ByteCodeFunction& fn) = delete;
    ByteCodeFunction operator=(const ByteCodeFunction& fn) = delete;

    ~ByteCodeFunction() {
        platform.MemoryFree(byteCode); //释放掉自己所有的字节码buffer
    }
};

//这一部分从编译器那边复制来，保持一致
class OpCode {
public:
    enum IOpCode
    {
        //运算符
        ADD,
        SUB,
        MUL,
        DIV,
        MOD,    // 取模

        // 单目运算符，弹出一个对象运算压回去
        NOT,    // 逻辑非
        NEG,    // 取负

        // 位运算符
        BIT_AND,    // 按位与
        BIT_OR,     // 按位或
        BIT_XOR,    // 按位异或
        BIT_NOT,    // 按位取反
        SHL,        // 左移
        SHR,         // 右移

        //逻辑运算符 弹出两个对象然后进行比较结果的BOOL对象压入栈
        EQUAL,
        NOT_EQUAL,
        LOWER_EQUAL,
        GREATER_EQUAL,
        LOWER,
        GREATER,
        AND,
        OR,

        //栈保护指令
        SCOPE_PUSH, //创建新的作用域帧 带一个4字节操作数表示作用域字节码大小 ，尾部1字节表示控制流接收类型（break/contine）
        BREAK, //强制弹出并退出作用域帧，类似break
        CONTINUE, //重置当前作用域运行指针，类似continue
        POP, //弹出并丢弃一个值
        //栈保护指令将当前栈指针压入一个独立的记录栈中用于恢复

        //逻辑操作

        //废弃 SP_LD, //将栈指针PTR压到栈中
        //废弃 MOV_SP, //从栈弹出一个对象（PTR类型）然后将栈指针修改为这个对象的值
        PUSH_NUM, //压入常量数字（NUM类型，原生类型为double）
        PUSH_PTR, //压入PTR类型，原生类型long
        PUSH_STR, //压入字符串，操作数：索引(ushort)
        PUSH_BOOL, //压入布尔，操作数1字节，0或1
        PUSH_NULL, //压入null到栈（如果需要JIT编译就等效PUSH_NUM 0）,无操作数
        DUP_PUSH, //从栈顶拷贝一个VariableValue压入栈
        JMP,
        JMP_IF_FALSE,//从栈弹出1个对象，先出来的是条件
        CALLFUNC,
        RET,
        //TRY_ENTER指令包含：1b指令头+8b try块长度
        //[TRY_ENTER(TRYBLOCK.length + JMP.length)][TRYBLOCK][JMP(CATCH.length)][CATCH];
        TRY_ENTER, //操作数包含：try块+JMP指令大小，发生异常后自动修改EP到目标
        //TRY_END无操作数
        TRY_END, //暂时不需要
        
        THROW, //弹出一个元素用于异常处理

        //变量管理

        NEW_OBJ,
        NEW_ARR,
        STORE, //从栈弹出两个VariableValue，先后a，b，将a的VariableValue内的引用/值修改为b的值
        STORE_LOCAL, //自带变量名索引，直接存入局部变量，带有一个2字节操作数表示字符串常量池索引
        DEF_LOCAL, //弹出一个字符串，从局部符号表创建一个变量占位，默认值为NULL
        LOAD_LOCAL, //弹出一个字符串，从全局符号表创建一个变量占位，默认值为NULL
        DEL_DEF, //从栈弹出一个VariableValue，从全局符号表删除给定名称的值
        LOAD_VAR,  //从栈弹出一个字符串，从全局/局部符号表寻找变量将值压入栈
        GET_FIELD, //获取对象属性值，一个桥接VariableValue(type=BRIDGE)指向成员VariableValue的指针
        //GET_FIELD返回的VariableValue，obj.sub中，sub的值是 get_field.bridge->ref
        //STORE指令需要判断一下是不是桥接类型

        //常量池使用
        CONST_STR, //字符串常量,Unicode表示;指令结构：（1byte头+4byte长度+内容）
    };

    static uint8_t instructionSize[];
};

