using ScriptRuntime.Core;
using System;
using System.Numerics;
using System.Runtime.CompilerServices;
using System.Text;

namespace CompileLab
{
    internal class Program
    {
        static void Main(string[] args)
        {
            if (args.Length > 0)
            {
                var code = File.ReadAllText(args[0].Replace("\"", ""));
                code = StringUtils.ClearMultiSpace(code);
                code = StringUtils.RemoveSingleLineComments(code);
                var tokens = Lexer.SplitTokens(code);
                ASTNode ast = Parser.BuildASTByTokens(tokens);

                Console.WriteLine(SyntaxUtils.GetASTString(ast));


                var comp = new Compiler();
                var result = comp.FullCompile(ast);
                

                foreach (var cc in result)
                {
                    Console.WriteLine("FuncName: " + cc.Key);
                    Console.WriteLine("Args: " + string.Join(",", cc.Value.Item1));
                    Console.WriteLine("bytecode: [" + string.Join(",", cc.Value.Item2) + "]");
                    Console.WriteLine("asm: \r\n" + cc.Value.Item4);
                    foreach (var map in cc.Value.Item3)
                    {
                        Console.WriteLine($"offset:{map.offset} <-> line:{map.line}");
                    }
                }

                byte[] packed = Compiler.PackFunction(comp.ConstString,result);

                Console.WriteLine("packed: [" + string.Join(",", packed) + "]");


                string outputName = args[0].Substring(0,args[0].Length - args[0].Split(".").Last().Length - 1) + ".nejs";
                string mapOutputName = outputName + ".map";
                File.WriteAllBytes(outputName, packed);
                StringBuilder mapbuf = new StringBuilder();
                mapbuf.Append($"{outputName.Replace("\\", "/").Split('/').Last()} {result.Count}\n");
                foreach(var cc in result)
                {
                    mapbuf.Append($"{cc.Key} {cc.Value.Item3.Count}\n");
                    foreach (var map in cc.Value.Item3)
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
                    Console.WriteLine("Args: " + string.Join(",", cc.Value.Item1));
                    Console.WriteLine("bytecode: [" + string.Join(",", cc.Value.Item3) + "]");
                    Console.WriteLine("asm: \r\n" + cc.Value.Item4);
                }
                //VariableValue result = new VM().RunByteCode(byteCode);
                //Console.WriteLine("result: " +  result.Value);

                byte[] packed = Compiler.PackFunction(comp.ConstString,result);

                Console.WriteLine("packed: [" + string.Join(",", packed) + "]");
            }
        }
    }

}


