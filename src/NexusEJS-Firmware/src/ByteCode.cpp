#include "ByteCode.h"

uint8_t OpCode::instructionSize[] = {
    1,  // ADD
    1,  // SUB
    1,  // MUL
    1,  // DIV
    1,  // MOD
    1,  // NOT
    1,  // NEG
    1,  // BIT_AND
    1,  // BIT_OR
    1,  // BIT_XOR
    1,  // BIT_NOT
    1,  // SHL
    1,  // SHR
    1,  // EQUAL
    1,  // NOT_EQUAL
    1,  // LOWER_EQUAL
    1,  // GREATER_EQUAL
    1,  // LOWER
    1,  // GREATER
    1,  // AND
    1,  // OR
    6,  // SCOPE_PUSH
    1,  // BREAK
    1,  // CONTINUE
    1,  // POP
    9,  // PUSH_NUM
    9,  // PUSH_PTR
    3,  // PUSH_STR
    2,  // PUSH_BOOL
    1,  // PUSH_NULL
    1,  // DUP_PUSH
    5,  // JMP
    5,  // JMP_IF_FALSE
    2,  // CALLFUNC
    1,  // RET
    5,  // TRY_ENTER
    1,  // TRY_END
    1,  // THROW
    1,  // NEW_OBJ
    1,  // NEW_ARR
    1,  // STORE
    3,  // STORE_LOCAL
    3,  // DEF_LOCAL
    3,  // LOAD_LOCAL
    1,  // DEL_DEF
    1,  // LOAD_VAR
    1,  // GET_FIELD
    0, // CONST_STR (标记为0表示让分支自己处理变长)
};

 