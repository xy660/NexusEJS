/*
 * Copyright 2025 xy660
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
using ScriptRuntime.Core;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;


public static class SyntaxUtils
{
    public static string SymbolMap = "+-*/||&&%!.^\\=><:~";
    public static List<string> MathSymbol = new List<string>()
    {
        "+",
        "-",
        "*",
        "/",
    };
    public static List<string> LogicSymbol = new List<string>()
    {
        "==",
        "||",
        "&&",
        "!"
    };
    public static string ClipTokenString(int start, int count,List<Token> ASTParseStream)
    {
        StringBuilder sb = new StringBuilder();
        for (int i = start; i < start + count - 1; i++)
        {
            if (i < ASTParseStream.Count && i >= 0)
            {
                if (ASTParseStream[i].tokenType == TokenType.Part)
                {
                    sb.Append($"({ASTParseStream[i].raw})");
                }
                else if (ASTParseStream[i].tokenType == TokenType.IndexLabel)
                {
                    sb.Append($"[{ASTParseStream[i].raw}]");
                }
                else if (ASTParseStream[i].tokenType == TokenType.CodeBlock)
                {
                    sb.Append("{" + ASTParseStream[i].raw + "}");
                }
                else
                {
                    sb.Append(ASTParseStream[i].raw);
                }
                sb.Append(' ');
            }
        }
        return sb.ToString();
    }
    public static string GetASTString(ASTNode node, string indent = "", bool isLast = true,StringBuilder buf = null)
    {
        StringBuilder sb = buf is null ? new StringBuilder() : buf;
        // 打印当前节点
        sb.Append(indent);
        if (isLast)
        {
            sb.Append("└─ ");
            indent += "   ";
        }
        else
        {
            sb.Append("├─ ");
            indent += "│  ";
        }
        sb.AppendLine($"{node.NodeType}: {node.Raw}  line:{node.line}");

        // 递归打印子节点
        for (int i = 0; i < node.Childrens.Count; i++)
        {
            GetASTString(node.Childrens[i], indent, i == node.Childrens.Count - 1,sb);
        }
        return sb.ToString();
    }
}
