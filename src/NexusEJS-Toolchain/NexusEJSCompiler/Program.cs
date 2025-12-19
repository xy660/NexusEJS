using ScriptRuntime.Core;
using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;

namespace CompileLab
{
    internal class Program
    {
        static bool DetailOutput = false;
        static void Main(string[] args)
        {
            if (args.Length > 0)
            {
                foreach (var arg in args)
                {
                    if(arg == "--detail")
                    {
                        DetailOutput = true;
                    }
                }
                var code = File.ReadAllText(args[0].Replace("\"", ""));
                //code = StringUtils.ClearMultiSpace(code);
                code = StringUtils.RemoveComments(code);
                var tokens = Lexer.SplitTokens(code);
                ASTNode ast = Parser.BuildASTByTokens(tokens);

                Console.WriteLine(SyntaxUtils.GetASTString(ast));


                var comp = new Compiler();
                var result = comp.FullCompile(ast);

                if (DetailOutput)
                {
                    foreach (var cc in result)
                    {
                        Console.WriteLine("FuncName: " + cc.Key);
                        Console.WriteLine("Args: " + string.Join(",", cc.Value.arguments));
                        Console.WriteLine("bytecode: [" + string.Join(",", cc.Value.bytecode) + "]");
                        Console.WriteLine("asm: \r\n" + cc.Value.asm);
                        foreach (var map in cc.Value.mapper)
                        {
                            Console.WriteLine($"offset:{map.offset} <-> line:{map.line}");
                        }
                    }
                }
                


                string outputName = args[0].Substring(0,args[0].Length - args[0].Split(".").Last().Length - 1) + ".nejs";
                string packageName = Path.GetFileNameWithoutExtension(outputName);
                string mapOutputName = outputName + ".map";

                byte[] packed = Compiler.PackFunction(packageName,comp.ConstString, result);

                if(DetailOutput) 
                    Console.WriteLine("packed: [" + string.Join(",", packed) + "]");

                File.WriteAllBytes(outputName, packed);
                StringBuilder mapbuf = new StringBuilder();
                mapbuf.Append($"{outputName.Replace("\\", "/").Split('/').Last()} {result.Count}\n");
                foreach(var cc in result)
                {
                    mapbuf.Append($"{cc.Key} {cc.Value.mapper.Count}\n");
                    foreach (var map in cc.Value.mapper)
                    {
                        mapbuf.Append($"{map.offset}|{map.line}\n");
                    }
                }
                Console.WriteLine();
                Console.WriteLine("\nFinished!");
                File.WriteAllText(mapOutputName, mapbuf.ToString());
                Console.WriteLine($"output:{outputName}");
                Console.WriteLine($"map:{mapOutputName}");
                return;
            }

            Console.WriteLine("NexusEJS Interactive Compiler\r\n");
            while (true)
            {
                Console.Write(">> ");
                var code = Console.ReadLine();
                ASTNode ast = Parser.BuildASTByTokens(Lexer.SplitTokens(code));

               
                var comp = new Compiler();
                var result = comp.FullCompile(ast);
                foreach(var cc in result)
                {
                    Console.WriteLine("FuncName: " + cc.Key);
                    Console.WriteLine("Args: " + string.Join(",", cc.Value.arguments));
                    Console.WriteLine("bytecode: [" + string.Join(",", cc.Value.bytecode) + "]");
                    Console.WriteLine("asm: \r\n" + cc.Value.asm);
                }
                //VariableValue result = new VM().RunByteCode(byteCode);
                //Console.WriteLine("result: " +  result.Value);

                byte[] packed = Compiler.PackFunction("unnamed_package",comp.ConstString,result);

                Console.WriteLine("packed: [" + string.Join(",", packed) + "]");
            }
        }
    }

}


