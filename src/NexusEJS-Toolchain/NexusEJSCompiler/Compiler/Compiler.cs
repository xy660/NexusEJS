using nejsc.Utils;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Reflection.Metadata.Ecma335;
using System.Reflection;

namespace ScriptRuntime.Core
{

    public class CompileException : Exception
    {
        public uint line;
        public string message;
        public CompileException(uint line, string message) : base($"{message} line:{line}") 
        {
            this.line = line;
            this.message = message;
        }
    }

    //地址都是相对地址

    public class FunctionInfo
    {
        public string[] arguments;
        public string name;
        public List<(uint offset, uint line)> mapper;
        public byte[] bytecode;
        public ushort[] outsizeSymbolsId;
        public string asm;

        public FunctionInfo(string name,string[] arguments, List<(uint offset, uint line)> mapper, byte[] bytecode, ushort[] outsizeSym,string asm)
        {
            this.name = name;
            this.arguments = arguments;
            this.mapper = mapper;
            this.bytecode = bytecode;
            this.outsizeSymbolsId = outsizeSym;
            this.asm = asm;
        }
    }

    class Compiler
    {
        public static ushort Version = 5;
        enum OpCode
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
            SCOPE_PUSH, //创建新的作用域帧 带一个4字节操作数表示作用域字节码大小，尾部1字节为控制流类型
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
            JMP_IF_TRUE,
            CALLFUNC,
            RET,
            //TRY_ENTER指令包含：1b指令头+8b try块长度，由VM根据长度计算出catch块起点绝对坐标
            TRY_ENTER, //标记try起点，压入异常处理程序栈（一个独立的TryCatch记录栈，异常发生后弹出异常处理程序栈最顶上的处理程序跳转）
                       //TRY_END无操作数
            TRY_END, //从异常处理栈弹出，标记try块结束，执行此指令后需要一个JMP跳过catch块
                     //[TRY_ENTER(tryBlock.length)][tryBlock][TRY_END][JMP(catchBlock.length)][catchBlock]
            THROW, //弹出一个元素用于异常处理


            //变量管理

            NEW_OBJ,
            NEW_ARR,
            STORE, //从栈弹出两个VariableValue，先后a，b，将a的VariableValue内的引用/值修改为b的值
            STORE_LOCAL,
            DEF_LOCAL, //弹出一个字符串，从局部符号表创建一个变量占位，默认值为NULL
            LOAD_LOCAL, //从本地索引表加载一个变量
            DEL_DEF, //从栈弹出一个VariableValue，从全局符号表删除给定名称的值
            LOAD_VAR,  //从栈弹出一个字符串，从全局/局部符号表寻找变量将值压入栈
            GET_FIELD, //获取对象属性值，一个桥接VariableValue(type=BRIDGE)指向成员VariableValue的指针
                       //GET_FIELD返回的VariableValue，obj.sub中，sub的值是 get_field.bridge->ref
                       //STORE指令需要判断一下是不是桥接类型

            //常量池使用
            CONST_STR, //字符串常量,Unicode表示;指令结构：（1byte头+4byte长度+内容）
        }

        //作用域帧控制流类型
        enum ScopeType
        {
            NONE = 0,
            BREAK = 1 << 0,
            CONTINUE = 1 << 1,
            TRYCATCH = 1 << 2,
        }
        MemoryStream ms = new MemoryStream();
        StringBuilder sb = new StringBuilder();

        // <offset,line>映射，由最初的Compiler()无参构造，有参的同一传参
        //Dictionary<uint,uint> OffsetLineMapper = new Dictionary<uint,uint>();
        public List<(uint offset, uint line)> OffsetLineMapper;
        uint baseOffset = 0;

        bool scopeCommand = true; //BlockCode节点是否需要单独作用域
        Dictionary<string, OpCode> BinaryOpMapper = new Dictionary<string, OpCode>()
    {
    // 算术运算符
    {"+", OpCode.ADD},
    {"-", OpCode.SUB},
    {"*", OpCode.MUL},
    {"/", OpCode.DIV},

    // 比较运算符
    {"==", OpCode.EQUAL}, //严格比较（NexusEJS专门的特性）
    {"!=", OpCode.NOT_EQUAL},
    {"===", OpCode.EQUAL}, //这两个为了兼容传统js代码，但实际上与==一样都是严格比较
    {"!==", OpCode.NOT_EQUAL},
    {"<=", OpCode.LOWER_EQUAL},
    {">=", OpCode.GREATER_EQUAL},
    {"<", OpCode.LOWER},
    {">", OpCode.GREATER},

    // 逻辑运算符
    {"&&", OpCode.AND},
    {"||", OpCode.OR},
    {"%", OpCode.MOD},
    //{"**", OpCode.POWER},

    // 位运算符
    {"&", OpCode.BIT_AND},
    {"|", OpCode.BIT_OR},
    {"^", OpCode.BIT_XOR},
    {"<<", OpCode.SHL},
    {">>", OpCode.SHR}
    };

        Dictionary<string, OpCode> UnaryOpMapper = new Dictionary<string, OpCode>()
        {

        };

        // 栈净影响（正数表示增长，负数表示减少，0表示不变）
        Dictionary<OpCode, int> stackNetEffect = new Dictionary<OpCode, int>
{
    // 二元运算符：弹出2个，压入1个 → 净变化-1
    { OpCode.ADD, -1 },
    { OpCode.SUB, -1 },
    { OpCode.MUL, -1 },
    { OpCode.DIV, -1 },
    { OpCode.MOD, -1 },
    { OpCode.BIT_AND, -1 },
    { OpCode.BIT_OR, -1 },
    { OpCode.BIT_XOR, -1 },
    { OpCode.SHL, -1 },
    { OpCode.SHR, -1 },
    { OpCode.EQUAL, -1 },
    { OpCode.NOT_EQUAL, -1 },
    { OpCode.LOWER_EQUAL, -1 },
    { OpCode.GREATER_EQUAL, -1 },
    { OpCode.LOWER, -1 },
    { OpCode.GREATER, -1 },
    { OpCode.AND, -1 },
    { OpCode.OR, -1 },

    // 单目运算符：弹出1个，压入1个 → 净变化0
    { OpCode.NOT, 0 },
    { OpCode.NEG, 0 },
    { OpCode.BIT_NOT, 0 },

    // 栈保护指令
    { OpCode.BREAK, 0 },
    { OpCode.SCOPE_PUSH, 0 },
    { OpCode.CONTINUE, 0 },
    { OpCode.POP, -1 },

    // 压入指令：净增加1个
    { OpCode.PUSH_NUM, 1 },
    { OpCode.PUSH_PTR, 1 },
    { OpCode.PUSH_STR, 1 },
    { OpCode.PUSH_BOOL, 1 },
    { OpCode.PUSH_NULL, 1 },
    { OpCode.DUP_PUSH, 1 },

    // 跳转指令
    { OpCode.JMP, 0 },
    { OpCode.JMP_IF_FALSE, -1 },
    { OpCode.JMP_IF_TRUE, -1 },

    // 函数调用和返回
    { OpCode.CALLFUNC, 1 },  // 一定会返回一个值，如果没return就返回null
    { OpCode.RET, -1 },      // 至少弹出返回值

    // 异常处理
    { OpCode.TRY_ENTER, 0 },
    { OpCode.TRY_END, 0 },
    { OpCode.THROW, -1 },

    // 变量管理
    { OpCode.STORE, -1 }, //弹出两个，将被赋值的VariableValue栈值压回去
    { OpCode.STORE_LOCAL, 0 }, //弹出一个，压入一个
    { OpCode.DEF_LOCAL, 0 },
    { OpCode.LOAD_LOCAL, 1 },
    { OpCode.DEL_DEF, -1 },
    { OpCode.LOAD_VAR, 0 },  // 弹出变量名，从全局符号表寻找，压入变量值
    {OpCode.NEW_ARR,1 },
    {OpCode.NEW_OBJ,1 },
    {OpCode.GET_FIELD,-1 }, //弹出一个字符串+一个对象，然后压入新的prop

    {OpCode.CONST_STR,0 },

};

        // 指令在字节码中的体积（字节）
        Dictionary<OpCode, int> instructionSize = new Dictionary<OpCode, int>
{
    // 无操作数指令：1字节
    { OpCode.ADD, 1 },
    { OpCode.SUB, 1 },
    { OpCode.MUL, 1 },
    { OpCode.DIV, 1 },
    { OpCode.MOD, 1 },
    { OpCode.NOT, 1 },
    { OpCode.NEG, 1 },
    { OpCode.BIT_AND, 1 },
    { OpCode.BIT_OR, 1 },
    { OpCode.BIT_XOR, 1 },
    { OpCode.BIT_NOT, 1 },
    { OpCode.SHL, 1 },
    { OpCode.SHR, 1 },
    { OpCode.EQUAL, 1 },
    { OpCode.NOT_EQUAL, 1 },
    { OpCode.LOWER_EQUAL, 1 },
    { OpCode.GREATER_EQUAL, 1 },
    { OpCode.LOWER, 1 },
    { OpCode.GREATER, 1 },
    { OpCode.AND, 1 },
    { OpCode.OR, 1 },
    { OpCode.SCOPE_PUSH, 6 },
    { OpCode.BREAK, 1 },
    { OpCode.CONTINUE, 1 },
    { OpCode.POP, 1 },
    { OpCode.PUSH_NULL, 1 },
    { OpCode.DUP_PUSH, 1 },
    { OpCode.TRY_END, 1 },
    { OpCode.THROW, 1 },
    { OpCode.RET, 1 },

    // 带操作数指令
    { OpCode.PUSH_NUM, 9 },      // 1字节头 + 8字节double
    { OpCode.PUSH_PTR, 9 },      // 1字节头 + 8字节long
    { OpCode.PUSH_BOOL, 2 },     // 1字节头 + 1字节bool
    { OpCode.PUSH_STR, 3 },       //1字节头 + 2字节ushort索引
    { OpCode.CONST_STR, -1 },     // 1字节头 + 4字节长度 + 变长内容，标记为-1表示变长
    { OpCode.JMP, 5 },           // 1字节头 + 4字节地址
    { OpCode.JMP_IF_FALSE, 5 },  // 1字节头 + 4字节地址
    { OpCode.JMP_IF_TRUE, 5 },  // 1字节头 + 4字节地址
    { OpCode.TRY_ENTER, 5 },     // 1字节头 + 4字节地址
    { OpCode.CALLFUNC, 2 },      // 1字节头 + 1字节参数数量
    { OpCode.STORE, 1 },
    { OpCode.STORE_LOCAL, 3 },  //1字节头 + 2字节索引
    { OpCode.DEF_LOCAL, 3 },
    { OpCode.LOAD_LOCAL, 3 }, //1字节头+2字节索引
    { OpCode.DEL_DEF, 1 },
    { OpCode.LOAD_VAR, 1 },
    { OpCode.GET_FIELD,1 },
    { OpCode.NEW_ARR,1 },
    { OpCode.NEW_OBJ,1 },
};


        public int CalculateStackNetEffect(byte[] bytecode)
        {
            int effect = 0;
            int index = 0;
            while (index < bytecode.Length)
            {
                OpCode op = (OpCode)bytecode[index];
                int skipSize = instructionSize[op];
                if (op == OpCode.CONST_STR)
                {
                    int len = BitConverter.ToInt32(bytecode, index + 1);
                    skipSize = 1 + sizeof(int) + len * sizeof(char);
                }
                else if (op == OpCode.CALLFUNC)
                {
                    effect -= bytecode[index + 1];
                }
                else
                {
                    effect += stackNetEffect[op];
                }
                index += skipSize;
            }
            return effect;
        }

        public List<string> GetLocalVariableDefines(byte[] bytecode)
        {
            List<string> defines = new List<string>();
            List<string> constStr = new List<string>();
            int index = 0;
            int prevIndex = -1;
            while (index < bytecode.Length)
            {
                OpCode op = (OpCode)bytecode[index];
                int skipSize = instructionSize[op];
                if (op == OpCode.CONST_STR)
                {
                    //int len = BitConverter.ToInt32(bytecode, index + 1);
                    //skipSize = 1 + sizeof(int) + len * sizeof(char);
                    //string str = Encoding.Unicode.GetString(bytecode, index + 5, len);
                    //defines.Add(str);
                }
                else if (op == OpCode.DEL_DEF) //已经被删过的
                {
                    ushort constIndex = BitConverter.ToUInt16(bytecode, prevIndex + 1);
                    string defString = ConstString[constIndex];
                    defines.Remove(defString);
                }
                else if (op == OpCode.DEF_LOCAL)
                {
                    //DEF_LOCAL上一条指令一定是PUSH_STR
                    ushort constIndex = BitConverter.ToUInt16(bytecode, prevIndex + 1);
                    string defString = ConstString[constIndex];
                    defines.Add(defString);
                }
                prevIndex = index;
                index += skipSize;
            }
            return defines;
        }

        public List<string> ConstString = new List<string>();

        public List<string> LocalVariableDefines = new List<string>();

        //编译出来的方法体
        public static Dictionary<string, FunctionInfo> functions = new();

        public Compiler()
        {
            OffsetLineMapper = new List<(uint offset, uint line)>();
        }

        public class CompilationContext
        {
            public List<string> ConstStr { get; set; }
            public List<string> LocalVars { get; set; }
            public List<(uint Offset, uint Line)> OffsetLineMapper { get; set; }
            public uint BaseOffset { get; set; }

            public CompilationContext(
                List<string> conststr,
                List<string> localVars,
                List<(uint offset, uint line)> offsetLineMapper,
                uint baseOffset)
            {
                ConstStr = conststr;
                LocalVars = localVars;
                OffsetLineMapper = offsetLineMapper;
                BaseOffset = baseOffset;
            }
        }

        public Compiler(CompilationContext context)
        {
            OffsetLineMapper = context.OffsetLineMapper;
            ConstString = context.ConstStr;
            LocalVariableDefines = context.LocalVars;
            baseOffset = context.BaseOffset;
        }

        //将字节码数据链接成一个可执行包
        public static byte[] PackFunction(string packageName,List<string> ConstStringPool, Dictionary<string, FunctionInfo> functions)
        {
            //[magic]
            //[ushort版本号]
            //[ushort包名长度][包名]
            //[常量池字符串数量(uint)]{[字符串长度(uint)][unicode字符串]...}
            //{
            //  [函数名字符串id][参数数量byte]{[参数1字符串id]...}
            //  [外部符号id数量ushort]{[外部符号id]...}
            //  [字节码长度int][字节码]
            //}

            using (var ms = new MemoryStream())
            {
                ms.Write(new byte[] { 0x78, 0x79, 0x78, 0x79 }); //魔数 "xyxy"

                //写入版本号
                ms.Write(BitConverter.GetBytes(Version));
                
                //写入包名
                var packageNameData = Encoding.UTF8.GetBytes(packageName);
                ms.Write(BitConverter.GetBytes((ushort)packageNameData.Length));
                ms.Write(packageNameData);

                //写入常量字符串表
                ms.Write(BitConverter.GetBytes(ConstStringPool.Count));
                foreach(var cstr in ConstStringPool)
                {
                    var strData = Encoding.UTF8.GetBytes(cstr);
                    ms.Write(BitConverter.GetBytes(strData.Length));
                    ms.Write(strData);
                }

                foreach (var func in functions)
                {
                    //写入函数名
                    ms.Write(BitConverter.GetBytes((ushort)ConstStringPool.IndexOf(func.Key)));
                    //写入参数表
                    ms.WriteByte((byte)func.Value.arguments.Length);
                    for (int i = 0; i < func.Value.arguments.Length; i++)
                    {
                        ms.Write(BitConverter.GetBytes((ushort)ConstStringPool.IndexOf(func.Value.arguments[i])));
                    }
                    //写入外部符号表
                    ms.Write(BitConverter.GetBytes((ushort)func.Value.outsizeSymbolsId.Length));
                    foreach(var outSymId in func.Value.outsizeSymbolsId)
                    {
                        ms.Write(BitConverter.GetBytes(outSymId));
                    }
                    //写入字节码
                    ms.Write(BitConverter.GetBytes(func.Value.bytecode.Length));
                    ms.Write(func.Value.bytecode);
                }
                return ms.ToArray();
            }

        }

        public string GenerateName()
        {
            var time = (DateTime.UtcNow.Ticks % 100000000).ToString("X8"); // 8位十六进制
            var random = new Random().Next(1000, 9999); // 4位随机数
            return $"{time}{random}";
        }

        ushort GetOrCreateConstStringId(string str)
        {
            ushort index = 0; //字符串常量池索引
            if (!ConstString.Contains(str))
            {
                ConstString.Add(str);
                index = (ushort)(ConstString.Count - 1);
            }
            else
            {
                index = (ushort)ConstString.IndexOf(str);
            }
            return index;
        }
        void Emit(OpCode opcode, object param = null, object param2 = null)
        {
            switch (opcode)
            {
                /*
                case OpCode.ADD:
                case OpCode.SUB:
                case OpCode.MUL:
                case OpCode.DIV:
                case OpCode.EQUAL:
                case OpCode.NOT_EQUAL:
                case OpCode.GREATER_EQUAL:
                case OpCode.LOWER_EQUAL:
                case OpCode.GREATER:
                case OpCode.LOWER:
                case OpCode.AND:
                case OpCode.OR:
                case OpCode.DEF_LOCAL:
                case OpCode.DEF_GLOBAL:
                case OpCode.STORE:
                    ms.WriteByte((byte)(int)opcode);
                    break;
                */
                case OpCode.JMP:
                case OpCode.JMP_IF_TRUE:
                case OpCode.JMP_IF_FALSE:
                    ms.WriteByte((byte)(int)opcode);
                    ms.Write(BitConverter.GetBytes((int)param)); //指针的底层使用long存储
                    break;
                case OpCode.CALLFUNC:
                    ms.WriteByte((byte)(int)opcode);
                    ms.WriteByte((byte)(int)param);
                    break;
                case OpCode.PUSH_NUM:
                    ms.WriteByte((byte)(int)opcode);
                    ms.Write(BitConverter.GetBytes((double)param));
                    break;
                case OpCode.CONST_STR:
                    ms.WriteByte((byte)(int)opcode);
                    ms.Write(BitConverter.GetBytes(((string)param).Length));
                    ms.Write(Encoding.Unicode.GetBytes((string)param));
                    break;
                case OpCode.LOAD_LOCAL:
                    {
                        ushort var_id = (ushort)(int)param;
                        ms.WriteByte((byte)(int)opcode);
                        ms.Write(BitConverter.GetBytes(var_id));
                        sb.Append($"[vid={var_id}]");
                        break;
                    }
                case OpCode.STORE_LOCAL:
                case OpCode.DEF_LOCAL:
                case OpCode.PUSH_STR:
                    {
                        ushort index = 0; //字符串常量池索引
                        if (!ConstString.Contains((string)param))
                        {
                            ConstString.Add((string)param);
                            index = (ushort)(ConstString.Count - 1);
                        }
                        else
                        {
                            index = (ushort)ConstString.IndexOf((string)param);
                        }
                        ms.WriteByte((byte)(int)opcode);
                        ms.Write(BitConverter.GetBytes(index));
                        sb.Append($"[sid={index}]");
                    }
                    break;
                case OpCode.PUSH_BOOL:
                    ms.WriteByte((byte)(int)opcode);
                    ms.WriteByte((bool)param ? (byte)1 : (byte)0);
                    break;
                case OpCode.SCOPE_PUSH:
                    ms.WriteByte((byte)(int)opcode);
                    ms.Write(BitConverter.GetBytes((int)param));
                    ms.WriteByte(param2 == null ? (byte)0 : (byte)(int)param2);
                    if (param2 != null)
                    {
                        sb.Append($"[{param2.ToString()}]");
                    }
                    break;
                case OpCode.TRY_ENTER:
                    ms.WriteByte((byte)(int)opcode);
                    ms.Write(BitConverter.GetBytes((int)param));
                    break;
                default:
                    ms.WriteByte((byte)(int)opcode);
                    break;
            }
            sb.Append(opcode.ToString());
            sb.Append(' ');
            if (param != null)
            {
                sb.Append(param);
            }
            sb.AppendLine();
        }

        void StackBalance(Compiler comp)
        {
            int effect = CalculateStackNetEffect(comp.ms.ToArray());
            for (int i = 0; i < effect; i++)
            {
                comp.Emit(OpCode.POP); //栈平衡
            }
        }

        public Dictionary<string, FunctionInfo> FullCompile(ASTNode ast)
        {
            functions.Clear();
            ASTNode mainEntry = new ASTNode(ASTNode.ASTNodeType.FunctionDefinition, "main_entry", ast.line);
            mainEntry.Childrens.Add(ast);
            var (byteCode, Asm) = Compile(mainEntry);
            //functions.Add("main_entry", (new string[0],byteCode, Asm)); //最终注册在外面的方法为main
            int effect = CalculateStackNetEffect(byteCode);
            return functions;
        }

        public (byte[], string) Compile(ASTNode ast)
        {
            //这几个节点不记录偏移量
            if (ast.NodeType != ASTNode.ASTNodeType.BlockCode &&
                ast.NodeType != ASTNode.ASTNodeType.FunctionDefinition)
            {
                OffsetLineMapper.Add(((uint)(ms.Position + baseOffset), ast.line));
                //Console.WriteLine($"{ast.NodeType} offset:{baseOffset + ms.Position} line:{ast.line}");
            }


            bool requireForEachChildren = true;
            if (ast.NodeType == ASTNode.ASTNodeType.IfStatement)
            {
                //[codition][JMP_IF_FALSE(trueBlock.size + JMP.size)][trueBlock][JMP(falseBlockSize)][falseBlock]
                //[codition][JMP_IF_FALSE(trueBlock.size)][trueBlock]
                ASTNode codition = ast.Childrens[0];
                ASTNode trueBlock = ast.Childrens[1];
                ASTNode? falseBlock = ast.Childrens.Count > 2 ? ast.Childrens[2] : null;

                Compile(codition); //先计算条件看看要不要跳转

                //隔离编译

                var trueBlockOffset = baseOffset + ms.Position + instructionSize[OpCode.JMP_IF_FALSE];
                var (compTrue, asmTrue) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, (uint)trueBlockOffset)).Compile(trueBlock);
                var falseBlockOffset = (uint)(trueBlockOffset + compTrue.Length + instructionSize[OpCode.JMP]);
                (byte[]? compFalse, string? asmFalse) = falseBlock == null ? (null, null) : new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, falseBlockOffset)).Compile(falseBlock);

                //如果有else块在true块后面就有一个跳过指令
                //这个跳过指令包含1字节头+8字节地址=9字节，需要考虑
                //int jmpCodeSize = falseBlock == null ? compTrue.Length : compTrue.Length + 9;
                int jmpCodeSize = falseBlock == null ? compTrue.Length : compTrue.Length + instructionSize[OpCode.JMP];
                Emit(OpCode.JMP_IF_FALSE, jmpCodeSize);

                ms.Write(compTrue);
                sb.AppendLine(asmTrue);

                if (compFalse != null) //如果有else块就生成else块
                {
                    Emit(OpCode.JMP, compFalse.Length); //对应顶上true块结尾跳过else块
                    ms.Write(compFalse);
                    sb.AppendLine(asmFalse);
                }

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Assignment) //赋值
            {
                Compile(ast.Childrens[0]); //运行左侧让引用/值存储到栈
                if (ast.Raw != "=") //如果是类似x+=N这种复合赋值的话，拆成x=x+N编译
                {
                    //取第一个作为运算符
                    Emit(OpCode.DUP_PUSH); //复制一个用于计算
                    Compile(ast.Childrens[1]);
                    Emit(BinaryOpMapper[ast.Raw.Substring(0, ast.Raw.Length - 1)]);

                }
                else
                {
                    Compile(ast.Childrens[1]); //直接赋值，运行右侧
                }
                Emit(OpCode.STORE); //先弹出右侧再弹出左侧
                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.WhileStatement)
            {
                var codition = ast.Childrens[0];
                var block = ast.Childrens[1];

                var coditionOffset = (uint)(baseOffset + ms.Position + instructionSize[OpCode.SCOPE_PUSH]);
                var (coditionCode, coditionAsm) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, coditionOffset)) { scopeCommand = false }.Compile(codition);
                var blockCodeOffset = (uint)(coditionOffset + coditionCode.Length + instructionSize[OpCode.JMP_IF_FALSE]);
                var (blockCode, blockAsm) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines ,OffsetLineMapper, blockCodeOffset)) { scopeCommand = false }.Compile(block);

                //进入作用域
                Emit(OpCode.SCOPE_PUSH,
                    coditionCode.Length + instructionSize[OpCode.JMP_IF_FALSE] +
                    blockCode.Length + instructionSize[OpCode.JMP], ScopeType.BREAK | ScopeType.CONTINUE);
                //生成while循环模板
                ms.Write(coditionCode);
                sb.Append(coditionAsm);

                //生成条件不成立跳过指令，包括循环体代码 正向跳转不需要包含自己
                Emit(OpCode.JMP_IF_FALSE, blockCode.Length + instructionSize[OpCode.JMP]);

                ms.Write(blockCode);
                sb.Append(blockAsm);

                //负向跳转需要包含自己大小
                Emit(OpCode.JMP, -(blockCode.Length + coditionCode.Length + instructionSize[OpCode.JMP] + instructionSize[OpCode.JMP_IF_FALSE]));


                requireForEachChildren = false;
            }
            //测试用力
            //for(let i = 0;i < 10;i++){if(i%2==0){println("not");continue;} println(i)}
            else if (ast.NodeType == ASTNode.ASTNodeType.ForStatement)
            {
                var enterPosition = LocalVariableDefines.Count;

                uint currentOffset = (uint)(baseOffset + ms.Length);

                var (initPartCode, initPartAsm) = new Compiler(new CompilationContext(ConstString, LocalVariableDefines, OffsetLineMapper, currentOffset)) { scopeCommand = false }.Compile(ast.Childrens[0]);

                ms.Write(initPartCode.ToArray());
                sb.Append(initPartAsm.ToString());

                var codition = ast.Childrens[1];
                var block = ast.Childrens[3];
                var step = ast.Childrens[2];

                //[INIT_PART]
                //[SCOPE_PUSH(BREAK)]
                //  [CODITION]
                //  [JMP_IF_FALSE]
                //  [SCOPE_PUSH(CONTINUE)]
                //      [BLOCK]
                //  [STEP_PART]
                //  [JMP(前面所有的)]
                var coditionOffset = (uint)(currentOffset + initPartCode.Length + instructionSize[OpCode.SCOPE_PUSH]);
                var (coditionCode, coditionAsm) = new Compiler(new CompilationContext(ConstString, LocalVariableDefines, OffsetLineMapper, coditionOffset)) { scopeCommand = false }.Compile(codition);
                var blockCodeOffset = (uint)(coditionOffset + coditionCode.Length + instructionSize[OpCode.JMP_IF_FALSE] + instructionSize[OpCode.SCOPE_PUSH]);
                var (blockCode, blockAsm) = new Compiler(new CompilationContext(ConstString, LocalVariableDefines, OffsetLineMapper, blockCodeOffset)) { scopeCommand = false }.Compile(block);
                var stepPartOffset = (uint)(blockCodeOffset + blockCode.Length);
                var stepPartCompiler = new Compiler(new CompilationContext(ConstString, LocalVariableDefines, OffsetLineMapper, stepPartOffset)) { scopeCommand = false };
                stepPartCompiler.Compile(step);
                StackBalance(stepPartCompiler); //对步进进行栈平衡消耗可能产生的废物
                var stepPartCode = stepPartCompiler.ms.ToArray();
                var stepPartAsm = stepPartCompiler.sb.ToString();


                int fullPartSize = instructionSize[OpCode.SCOPE_PUSH] * 2;
                fullPartSize += instructionSize[OpCode.JMP_IF_FALSE];
                fullPartSize += instructionSize[OpCode.JMP];
                fullPartSize += coditionCode.Length;
                fullPartSize += blockCode.Length;
                fullPartSize += stepPartCode.Length;

                int jifSize = instructionSize[OpCode.SCOPE_PUSH];
                jifSize += blockCode.Length;
                jifSize += stepPartCode.Length;
                jifSize += instructionSize[OpCode.JMP];

                int loopBlockSize = blockCode.Length;

                //SCOPE_PUSH指令的操作数Size不包含自身的大小，需要注意
                //这里也不能把JMP指令放到外层作用域
                int BreakScopeSize = fullPartSize - instructionSize[OpCode.SCOPE_PUSH];
                Emit(OpCode.SCOPE_PUSH, BreakScopeSize, ScopeType.BREAK);
                ms.Write(coditionCode);
                sb.Append(coditionAsm);
                Emit(OpCode.JMP_IF_FALSE, jifSize);
                Emit(OpCode.SCOPE_PUSH, loopBlockSize, ScopeType.CONTINUE);
                ms.Write(blockCode);
                sb.Append(blockAsm);
                ms.Write(stepPartCode);
                sb.Append(stepPartAsm);
                //往回跳转的时候复用Break层作用域
                Emit(OpCode.JMP, -(fullPartSize - instructionSize[OpCode.SCOPE_PUSH]));

                //清理可能出现的initPart定义变量
                LocalVariableDefines.RemoveRange(enterPosition, LocalVariableDefines.Count - enterPosition);

                /*
                //展开为while循环格式，然后调用while循环进行处理
                //for(init;codition;step){block}

                ASTNode whileForBlock = new ASTNode(ASTNode.ASTNodeType.BlockCode, "", ast.line);
                //编译初始化（要注意脱掉BlockCode包装，这里不需要额外作用域）
                whileForBlock.Childrens.AddRange(ast.Childrens[0].Childrens);

                whileForBlock.Childrens.Add(new ASTNode(ASTNode.ASTNodeType.WhileStatement, "", ast.line));

                //获取while表达式的Node，即最后一个元素
                int whileStatNodeIndex = whileForBlock.Childrens.Count - 1;

                whileForBlock.Childrens[whileStatNodeIndex].Childrens.Add(ast.Childrens[1]); //条件
                whileForBlock.Childrens[whileStatNodeIndex].Childrens.Add(new ASTNode(ASTNode.ASTNodeType.BlockCode, "", ast.line));
                whileForBlock.Childrens[whileStatNodeIndex].Childrens[1].Childrens.Add(ast.Childrens[3]); //Block
                whileForBlock.Childrens[whileStatNodeIndex].Childrens[1].Childrens.Add(ast.Childrens[2]); //步进
                Compile(whileForBlock);
                */

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.FunctionDefinition)
            {

                var funcName = ast.Raw == string.Empty ? "anon" + GenerateName() : ast.Raw;
                GetOrCreateConstStringId(funcName); //给函数名创建常量池id
                string[] args = new string[ast.Childrens.Count - 1];
                for (int i = 0; i < ast.Childrens.Count - 1; i++)
                {
                    GetOrCreateConstStringId(ast.Childrens[i].Raw); //给参数创建常量池id
                    args[i] = ast.Childrens[i].Raw;
                }
                //和其他方法仅共享常量池不共享offset记录和局部变量
                List<string> functionVariables = new List<string>();
                functionVariables.AddRange(args);
                var comp = new Compiler(new CompilationContext(ConstString,functionVariables, new List<(uint offset, uint line)>(), 0));
                var (byteCode, asm) = comp.Compile(ast.Childrens.Last());
                var InnerConstString = comp.ConstString;

                List<(uint offset, uint line)> offsetMapper = comp.OffsetLineMapper;


                //计算外部符号依赖
                var outsideSymbol = OutsideSymbolDetector.DetectOutletSymbol(ast);
                List<ushort> outsizeSymIds = new List<ushort>();
                foreach(var sym in outsideSymbol)
                {
                    outsizeSymIds.Add(GetOrCreateConstStringId(sym));
                }

                functions.Add(funcName, new FunctionInfo(funcName, args, offsetMapper, byteCode,outsizeSymIds.ToArray() , asm));
                //functions.Add(funcName, (args, comp.ms.ToArray(),offsetMapper, comp.sb.ToString()));

                if (ast.Raw == string.Empty) //如果是匿名函数，压入栈作为值传递
                {
                    Emit(OpCode.PUSH_STR, funcName);
                    Emit(OpCode.LOAD_VAR);
                }
                else
                {
                    //普通函数在当前这里生成一个let <Name> = <Name>形成闭包
                    Emit(OpCode.PUSH_STR, funcName);
                    Emit(OpCode.LOAD_VAR);
                    Emit(OpCode.STORE_LOCAL,funcName);
                    if (LocalVariableDefines.Contains(funcName))
                    {
                        throw new CompileException(ast.line, $"\"{funcName}\" has already been declared");
                    }
                    LocalVariableDefines.Add(funcName);
                }

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.CallFunction)
            {
                //string funcName = ast.Childrens[0].Raw;

                Compile(ast.Childrens[0]);

                //Emit(OpCode.PUSH_STR, funcName); //方法对象现在外面 
                //Emit(OpCode.LOAD_VAR);

                foreach (var argAst in ast.Childrens.Skip(1).Reverse()) //倒过来压栈，这样弹出就是正序
                {
                    Compile(argAst);
                }

                Emit(OpCode.CALLFUNC, ast.Childrens.Count - 1);
                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.UnaryOperation)
            {
                Compile(ast.Childrens[0]);
                if (ast.Raw == "++")
                {
                    Emit(OpCode.DUP_PUSH);
                    Emit(OpCode.PUSH_NUM, (double)1);
                    Emit(OpCode.ADD);
                    Emit(OpCode.STORE); //先弹出右侧再弹出左侧
                }
                else if (ast.Raw == "--")
                {
                    Emit(OpCode.DUP_PUSH);
                    Emit(OpCode.PUSH_NUM, (double)1);
                    Emit(OpCode.SUB);
                    Emit(OpCode.STORE); //先弹出右侧再弹出左侧
                }
                else if (ast.Raw == "!")
                {
                    Emit(OpCode.NOT);
                }
                else if (ast.Raw == "~")
                {
                    Emit(OpCode.BIT_NOT);
                }
                else if (ast.Raw == "-")
                {
                    Emit(OpCode.NEG);
                }
                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.LeftUnaryOperation)
            {
                Compile(ast.Childrens[0]);
                Emit(OpCode.DUP_PUSH);
                if (ast.Raw == "++")
                {
                    Emit(OpCode.PUSH_NUM, (double)1);
                    Emit(OpCode.ADD);
                }
                else if (ast.Raw == "--")
                {
                    Emit(OpCode.PUSH_NUM, (double)1);
                    Emit(OpCode.SUB);
                }
                Emit(OpCode.STORE); //先弹出右侧再弹出左侧
                Emit(OpCode.PUSH_NUM, (double)1);
                Emit(OpCode.SUB); //弹出老结果

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Array)
            {
                Emit(OpCode.NEW_ARR); //创建一个空数组

                if (ast.Childrens.Count > 0) //如果字面量不为空就循环调用add添加
                {
                    Emit(OpCode.DUP_PUSH); //下面的GET_FIELD会消耗ARR实例，复制一个引用
                    Emit(OpCode.PUSH_STR, "push"); //获取数组的add方法，然后逐个调用添加函数
                    Emit(OpCode.GET_FIELD);

                    for (int i = 0; i < ast.Childrens.Count - 1; i++)
                    {
                        Emit(OpCode.DUP_PUSH); //复制n-1个add方法用来调用
                    }


                    for (int i = 0; i < ast.Childrens.Count; i++)
                    {
                        Compile(ast.Childrens[i]);
                        Emit(OpCode.CALLFUNC, 1);
                        Emit(OpCode.POP); //丢弃add方法返回值
                    }
                }

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Object)
            {
                Emit(OpCode.NEW_OBJ);
                //复制N个对象引用，然后用来批量GET_FIELD隐式创建key赋值
                for (int i = 0; i < ast.Childrens.Count; i++)
                {
                    Emit(OpCode.DUP_PUSH);
                }
                for (int i = 0; i < ast.Childrens.Count; i++)
                {
                    Emit(OpCode.PUSH_STR, ast.Childrens[i].Childrens[0].Raw);
                    Emit(OpCode.GET_FIELD);
                    Compile(ast.Childrens[i].Childrens[1]); //编译value然后存储
                    Emit(OpCode.STORE);
                    Emit(OpCode.POP); //抛弃赋值的返回值
                }

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.ArrayLabel) //下标
            {
                //下标全部编译为get方法(BRIDGE类型获取)
                Compile(ast.Childrens[0]);
                Emit(OpCode.PUSH_STR, "get");
                Emit(OpCode.GET_FIELD);
                Compile(ast.Childrens[1]);
                Emit(OpCode.CALLFUNC, 1);

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.MemberAccess) //成员访问符
            {
                Compile(ast.Childrens[0]); //编译要访问的表达式
                Emit(OpCode.PUSH_STR, ast.Childrens[1].Raw);
                Emit(OpCode.GET_FIELD);

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.TryCatchStatement)
            {

                int catchVarImplSize = 0; //catch块序言大小（用于装载捕获的异常对象）
                catchVarImplSize += instructionSize[OpCode.SCOPE_PUSH];
                catchVarImplSize += instructionSize[OpCode.STORE_LOCAL];
                catchVarImplSize += instructionSize[OpCode.POP];

                

                //[TRY_ENTER(TRYBLOCK.length + JMP.length)][TRYBLOCK][JMP(CATCH.length + [加载指令].length)][(加载指令)][CATCH];
                uint tryBlockOffset = (uint)(baseOffset + ms.Position + instructionSize[OpCode.TRY_ENTER]);
                var (tryBlock, tryBlockAsm) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, tryBlockOffset)).Compile(ast.Childrens[0]);

                if (LocalVariableDefines.Contains(ast.Childrens[1].Raw))
                {
                    throw new CompileException(ast.line, $"\"{ast.Childrens[1].Raw}\" has already been declared");
                }
                LocalVariableDefines.Add(ast.Childrens[1].Raw);

                uint catchBlockOffset = (uint)(tryBlockOffset + tryBlock.Length + instructionSize[OpCode.JMP] + catchVarImplSize);
                var (catchBlock, catchBlockAsm) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, catchBlockOffset)).Compile(ast.Childrens[2]);
                //标记try块大小，最后那个JMP指令也算在try块
                Emit(OpCode.TRY_ENTER, tryBlock.Length + instructionSize[OpCode.JMP]);
                ms.Write(tryBlock);
                sb.AppendLine(tryBlockAsm);

                //跳过整个catch部分
                Emit(OpCode.JMP, catchBlock.Length + catchVarImplSize);

                //编译catch序言，触发异常后异常对象的引用会出现在栈顶，将其存入exception变量
                //创建新的作用域执行catch，大小：catch块+catch序言-SCOPE_PUSH指令大小
                Emit(OpCode.SCOPE_PUSH, catchBlock.Length + catchVarImplSize - instructionSize[OpCode.SCOPE_PUSH]);
                Emit(OpCode.STORE_LOCAL, ast.Childrens[1].Raw); //直接存入

                

                Emit(OpCode.POP); //弹出剩下的异常对象
                ms.Write(catchBlock);
                sb.AppendLine(catchBlockAsm);

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.LockStatement)
            {
                ASTNode transNode = new ASTNode(ASTNode.ASTNodeType.BlockCode, "", ast.line);
                ASTNode lockBlock = new ASTNode(ASTNode.ASTNodeType.BlockCode, "", ast.line);
                transNode.Childrens.Add(new ASTNode(ASTNode.ASTNodeType.CallFunction, "", ast.line)
                {
                    Childrens =
                {
                    new ASTNode(ASTNode.ASTNodeType.Identifier, "mutexLock",ast.line) ,
                    ast.Childrens[0]
                }
                });
                string catchName = "lckerr" + GenerateName();
                lockBlock.Childrens.Add(new ASTNode(ASTNode.ASTNodeType.TryCatchStatement, "", ast.line)
                {
                    Childrens =
                {
                    new ASTNode(ASTNode.ASTNodeType.BlockCode, "",ast.line)
                    {
                        Childrens =
                        {
                            ast.Childrens[1], //lock语句内部代码
                            new ASTNode(ASTNode.ASTNodeType.CallFunction, "",ast.Childrens[1].line) //正常执行完成释放锁
                            {
                                Childrens =
                                {
                                   new ASTNode(ASTNode.ASTNodeType.Identifier,"mutexUnlock",ast.Childrens[1].line),
                                    ast.Childrens[0]
                                }
                            }
                        }
                    },
                    new ASTNode(ASTNode.ASTNodeType.Identifier,catchName,ast.line),
                    new ASTNode(ASTNode.ASTNodeType.BlockCode, "",ast.line)
                    {
                        Childrens =
                        {
                            new ASTNode(ASTNode.ASTNodeType.CallFunction, "",ast.line) //发生异常释放锁
                            {
                                Childrens =
                                {
                                   new ASTNode(ASTNode.ASTNodeType.Identifier,"mutexUnlock",ast.line),
                                    ast.Childrens[0]
                                }
                            },
                            new ASTNode(ASTNode.ASTNodeType.ThrowStatement, "",ast.line) //重新抛出
                            {
                                Childrens =
                                {
                                    new ASTNode(ASTNode.ASTNodeType.Identifier,catchName,ast.line)
                                }
                            }
                        }
                    }
                }
                });

                transNode.Childrens.Add(lockBlock);

                //Console.WriteLine("Lock Transform AST:\r\n" + SyntaxUtils.GetASTString(lockBlock));

                Compile(transNode);

                requireForEachChildren = false;
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.BlockCode) //花括号作用域块
            {
                uint currentOffset = (uint)(baseOffset + ms.Length);
                //基偏移加上作用域指令
                if (scopeCommand) currentOffset += (uint)instructionSize[OpCode.SCOPE_PUSH];

                //记录当前的局部变量栈位置
                int enterPosition = LocalVariableDefines.Count;

                var blockBytecode = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, currentOffset)); //这个compiler用来存储并进行最终局部变量清理
                foreach (var expr in ast.Childrens)
                {
                    //单独编译每一条语句，对语句进行栈平衡
                    var (bytecode, asm) = new Compiler(new CompilationContext(ConstString,LocalVariableDefines, OffsetLineMapper, (uint)(currentOffset + blockBytecode.ms.Length))).Compile(expr);        
                    blockBytecode.ms.Write(bytecode); //存入最终筛选
                    blockBytecode.sb.AppendLine(asm);
                    StackBalance(blockBytecode);
                }



                if (scopeCommand) Emit(OpCode.SCOPE_PUSH, (int)blockBytecode.ms.Length); //进入作用域
                ms.Write(blockBytecode.ms.ToArray());
                sb.AppendLine(blockBytecode.sb.ToString());

                //清理内层变量(如果有SCOPE指令)
                if (scopeCommand) LocalVariableDefines.RemoveRange(enterPosition, LocalVariableDefines.Count - enterPosition);

                requireForEachChildren = false;
            }

            if (requireForEachChildren)
            {
                foreach (ASTNode node in ast.Childrens)
                {
                    Compile(node);
                }
            }

            if (ast.NodeType == ASTNode.ASTNodeType.BinaryOperation)
            {
                Emit(BinaryOpMapper[ast.Raw]);
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Number)
            {
                Emit(OpCode.PUSH_NUM, double.Parse(ast.Raw));
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.StringValue)
            {
                Emit(OpCode.PUSH_STR, ast.Raw);
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Boolean)
            {
                Emit(OpCode.PUSH_BOOL, ast.Raw == "true");
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.VariableDefination) //local变量创建
            {
                //Emit(OpCode.PUSH_STR, ast.Raw);
                Emit(OpCode.DEF_LOCAL,ast.Raw);

                if (LocalVariableDefines.Contains(ast.Raw))
                {
                    throw new CompileException(ast.line, $"\"{ast.Raw}\" has already been declared");
                }

                LocalVariableDefines.Add(ast.Raw); //新增局部变量
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.Identifier)
            {
                if (ast.Raw == "null") //处理null常量
                {
                    Emit(OpCode.PUSH_NULL);
                }
                else
                {
                    //判断是否为局部变量，局部变量走快速通道
                    if (LocalVariableDefines.Contains(ast.Raw))
                    {
                        GetOrCreateConstStringId(ast.Raw);
                        Emit(OpCode.LOAD_LOCAL, LocalVariableDefines.IndexOf(ast.Raw));
                    }
                    else
                    {
                        Emit(OpCode.PUSH_STR, ast.Raw);
                        Emit(OpCode.LOAD_VAR);
                    }
                }
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.ReturnStatement)
            {
                Emit(OpCode.RET);
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.BreakStatement)
            {
                Emit(OpCode.BREAK);
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.ContinueStatement)
            {
                Emit(OpCode.CONTINUE);
            }
            else if (ast.NodeType == ASTNode.ASTNodeType.ThrowStatement)
            {
                Emit(OpCode.THROW);
            }

            

            return (ms.ToArray(), sb.ToString());
        }
    }
}
