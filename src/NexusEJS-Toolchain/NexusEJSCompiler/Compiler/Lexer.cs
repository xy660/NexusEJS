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
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using static StringUtils;
using static SyntaxUtils;
using ScriptRuntime.Core;
using ScriptRuntime.Runtime;

namespace ScriptRuntime.Core
{
    public enum TokenType
    {
        Part,        // 括号内表达式
        CodeBlock,   // 代码块或者数组常量 {xxx;xxx;}
        Operator,    // +, -, *, /
        Idfefinder,  // 标识符、变量、关键字等
        IndexLabel,  // 数组下标[...]
        EOF,         // 输入结束
        Processed    // 解析完成的
    }

    public static class Lexer
    {
        public static readonly Dictionary<string, char> EscapeToChar = new Dictionary<string, char>
        {
            ["\\\\"] = '\\',  // 反斜杠
            ["\\\'"] = '\'',  // 单引号
            ["\\\""] = '\"',  // 双引号
            ["\\0"] = '\0',   // 空字符
            ["\\a"] = '\a',   // 警报
            ["\\b"] = '\b',   // 退格
            ["\\f"] = '\f',   // 换页
            ["\\n"] = '\n',   // 换行
            ["\\r"] = '\r',   // 回车
            ["\\t"] = '\t',   // 水平制表
            ["\\v"] = '\v',   // 垂直制表
        };

        public static readonly Dictionary<char, string> CharToEscape = new Dictionary<char, string>
        {
            ['\\'] = "\\\\",  // 反斜杠
            ['\''] = "\\\'",  // 单引号
            ['\"'] = "\\\"",  // 双引号
            ['\0'] = "\\0",   // 空字符
            ['\a'] = "\\a",   // 警报
            ['\b'] = "\\b",   // 退格
            ['\f'] = "\\f",   // 换页
            ['\n'] = "\\n",   // 换行
            ['\r'] = "\\r",   // 回车
            ['\t'] = "\\t",   // 水平制表
            ['\v'] = "\\v"    // 垂直制表
        };

        private static (Token, int, uint) FindNextToken(string syn, int index, uint currentLine)
        {
            int jmp = index;
            uint line = currentLine;

            // 跳过空白字符和注释
            while (jmp < syn.Length)
            {
                // 跳过空白字符
                while (jmp < syn.Length && char.IsWhiteSpace(syn[jmp]))
                {
                    if (syn[jmp] == '\n') line++;
                    jmp++;
                }

                // 检查是否有多行注释开始
                if (jmp + 1 < syn.Length && syn[jmp] == '/' && syn[jmp + 1] == '*')
                {
                    jmp += 2; // 跳过 /*

                    // 跳过多行注释内容
                    while (jmp + 1 < syn.Length && !(syn[jmp] == '*' && syn[jmp + 1] == '/'))
                    {
                        if (syn[jmp] == '\n') line++;
                        jmp++;
                    }

                    if (jmp + 1 >= syn.Length)
                    {
                        throw new SyntaxException("多行注释未闭合", syn);
                    }

                    jmp += 2; // 跳过 */
                }
                // 检查是否有单行注释
                else if (jmp + 1 < syn.Length && syn[jmp] == '/' && syn[jmp + 1] == '/')
                {
                    // 跳过单行注释直到行尾
                    while (jmp < syn.Length && syn[jmp] != '\n')
                    {
                        jmp++;
                    }
                }
                else
                {
                    break; // 没有注释了，继续解析token
                }
            }

            if (jmp >= syn.Length)
            {
                return (new Token("", TokenType.EOF) { line = line }, jmp, line);
            }

            char cur = syn[jmp];

            // 由第一个字符类型判断Token类型
            if (bracket.ContainsKey(cur)) // 括号部分
            {
                int end = BracketScan(syn, jmp, line);
                // 只提取括号内部的内容，不包含括号本身
                string bracketContent = syn.Substring(jmp + 1, end - jmp - 1);
                // 计算括号内容中的行数变化
                uint contentLineCount = CountLinesInContent(bracketContent, line);
                // Token的行号设置为括号开始的行号，返回的行号是内容结束后的行号
                return (new Token(bracketContent, bracketTokenType[cur]) { line = line }, end + 1, contentLineCount);
            }
            else if (cur == '{') // 代码块
            {
                int end = BracketScan(syn, jmp, line);
                // 只提取大括号内部的内容，不包含大括号本身
                string blockContent = syn.Substring(jmp + 1, end - jmp - 1);
                // 计算代码块中的行数变化
                uint contentLineCount = CountLinesInContent(blockContent, line);
                return (new Token(blockContent, TokenType.CodeBlock) { line = line }, end + 1, contentLineCount);
            }
            else if (cur == '"' || cur == '\'') // 字符串
            {
                int tmp = jmp + 1;
                bool escaped = false;
                uint stringLine = line; // 使用当前行号

                while (tmp < syn.Length)
                {
                    if (escaped)
                    {
                        escaped = false;
                        tmp++;
                        continue;
                    }

                    if (syn[tmp] == '\\')
                    {
                        escaped = true;
                    }
                    else if (syn[tmp] == cur)
                    {
                        break;
                    }
                    else if (syn[tmp] == '\n')
                    {
                        stringLine++;
                    }

                    tmp++;
                }

                if (tmp >= syn.Length || syn[tmp] != cur)
                {
                    throw new SyntaxException("字符串未闭合", syn);
                }

                string stringContent = syn.Substring(jmp, tmp - jmp + 1);
                return (new Token(stringContent, TokenType.Idfefinder) { line = line }, tmp + 1, stringLine);
            }
            else if (char.IsLetter(cur) || cur == '_') // 标识符
            {
                int tmp = jmp + 1;
                while (tmp < syn.Length && (char.IsLetterOrDigit(syn[tmp]) || syn[tmp] == '_'))
                {
                    tmp++;
                }
                string tokenStr = syn.Substring(jmp, tmp - jmp);
                return (new Token(tokenStr, TokenType.Idfefinder) { line = line }, tmp, line);
            }
            else if (char.IsDigit(cur)) // 数字
            {
                int tmp = jmp;

                // 处理十六进制数字 (0x 或 0X 开头)
                if (tmp + 1 < syn.Length && syn[tmp] == '0' && (syn[tmp + 1] == 'x' || syn[tmp + 1] == 'X'))
                {
                    tmp += 2;
                    while (tmp < syn.Length && IsHexDigit(syn[tmp]))
                    {
                        tmp++;
                    }
                    string hexStr = syn.Substring(jmp, tmp - jmp);
                    // 将十六进制转换为十进制字符串
                    try
                    {
                        long decimalValue = Convert.ToInt64(hexStr, 16);
                        return (new Token(decimalValue.ToString(), TokenType.Idfefinder) { line = line }, tmp, line);
                    }
                    catch
                    {
                        throw new SyntaxException($"无效的十六进制数字: {hexStr}", syn);
                    }
                }
                else
                {
                    // 处理十进制数字
                    while (tmp < syn.Length && (char.IsDigit(syn[tmp]) || syn[tmp] == '.'))
                    {
                        tmp++;
                    }
                    string tokenStr = syn.Substring(jmp, tmp - jmp);
                    return (new Token(tokenStr, TokenType.Idfefinder) { line = line }, tmp, line);
                }
            }
            else if (SymbolMap.Contains(cur.ToString())) // 运算符
            {
                int tmp = jmp;
                while (tmp < syn.Length && SymbolMap.Contains(syn[tmp].ToString()))
                {
                    tmp++;
                }
                return (new Token(syn.Substring(jmp, tmp - jmp), TokenType.Operator) { line = line }, tmp, line);
            }
            else if (cur == ';')
            {
                return (new Token(";", TokenType.EOF) { line = line }, jmp + 1, line);
            }
            else if (cur == ':')
            {
                return (new Token(":", TokenType.Operator) { line = line }, jmp + 1, line);
            }
            else
            {
                throw new SyntaxException($"未知字符: {cur}", syn);
            }
        }

        // 计算内容中的行数变化
        private static uint CountLinesInContent(string content, uint startLine)
        {
            uint lineCount = startLine;
            foreach (char c in content)
            {
                if (c == '\n')
                {
                    lineCount++;
                }
            }
            return lineCount;
        }

        public static List<Token> SplitTokens(string syntax, uint inheritLine = 1)
        {
            if (string.IsNullOrEmpty(syntax))
                return new List<Token> { new Token("", TokenType.EOF) { line = inheritLine } };

            // 只替换\r，保留\n用于行号计算
            syntax = syntax.Replace("\r", "");
            List<Token> tokens = new List<Token>();
            int index = 0;
            uint currentLine = inheritLine;

            while (index < syntax.Length)
            {
                (Token token, int nextIndex, uint newLine) = FindNextToken(syntax, index, currentLine);

                if (token.tokenType == TokenType.EOF && string.IsNullOrEmpty(token.raw))
                    break;

                tokens.Add(token);
                index = nextIndex;
                currentLine = newLine;
            }

            return tokens;
        }

        // 重载方法，保持向后兼容
        public static List<Token> SplitTokens(string syntax)
        {
            return SplitTokens(syntax, 1);
        }

        // 辅助方法：判断是否为十六进制数字字符
        private static bool IsHexDigit(char c)
        {
            return (c >= '0' && c <= '9') ||
                   (c >= 'a' && c <= 'f') ||
                   (c >= 'A' && c <= 'F');
        }

        // 返回括号的下一个平衡点
        public static int BracketScan(string syn, int start, uint currentLine)
        {
            Stack<char> bracketStack = new Stack<char>();
            bool inString = false;
            char stringStartChar = '\0';
            bool escaped = false;
            bool inComment = false; // 是否在多行注释中
            bool inSingleLineComment = false; // 是否在单行注释中

            for (int i = start; i < syn.Length; i++)
            {
                char currentChar = syn[i];

                // 处理转义字符（在字符串内）
                if (escaped)
                {
                    escaped = false;
                    continue;
                }

                // 处理注释
                if (!inString && !inComment && !inSingleLineComment)
                {
                    // 检查多行注释开始
                    if (i + 1 < syn.Length && currentChar == '/' && syn[i + 1] == '*')
                    {
                        inComment = true;
                        i++; // 跳过*字符
                        continue;
                    }
                    // 检查单行注释开始
                    else if (i + 1 < syn.Length && currentChar == '/' && syn[i + 1] == '/')
                    {
                        inSingleLineComment = true;
                        i++; // 跳过第二个/字符
                        continue;
                    }
                }

                // 处理多行注释结束
                if (inComment && i + 1 < syn.Length && currentChar == '*' && syn[i + 1] == '/')
                {
                    inComment = false;
                    i++; // 跳过/字符
                    continue;
                }

                // 单行注释在遇到换行时结束
                if (inSingleLineComment && currentChar == '\n')
                {
                    inSingleLineComment = false;
                }

                // 如果在注释中，跳过当前字符
                if (inComment || inSingleLineComment)
                {
                    continue;
                }

                if (currentChar == '\\' && inString)
                {
                    escaped = true;
                    continue;
                }

                // 处理字符串开始/结束
                if ((currentChar == '"' || currentChar == '\'') && !inString)
                {
                    inString = true;
                    stringStartChar = currentChar;
                }
                else if (inString && currentChar == stringStartChar)
                {
                    inString = false;
                    stringStartChar = '\0';
                }

                // 不在字符串中时处理括号
                if (!inString && !inComment && !inSingleLineComment)
                {
                    if (bracket.ContainsKey(currentChar))
                    {
                        bracketStack.Push(currentChar);
                    }
                    else if (bracket.ContainsValue(currentChar))
                    {
                        if (bracketStack.Count == 0 || bracket[bracketStack.Pop()] != currentChar)
                        {
                            throw new SyntaxException("括号不匹配", syn);
                        }

                        if (bracketStack.Count == 0)
                        {
                            return i;
                        }
                    }
                }
            }

            throw new SyntaxException("括号未闭合", syn);
        }

        // 处理转义字符（保持不变）
        public static string ProcessEscapeToChar(string str)
        {
            if (string.IsNullOrEmpty(str) || str.Length < 2) return str;

            var rmk = str.Substring(1, str.Length - 2).ToCharArray();
            StringBuilder sb = new StringBuilder();

            for (int i = 0; i < rmk.Length; i++)
            {
                if (i < rmk.Length - 1)
                {
                    var s2 = $"{rmk[i]}{rmk[i + 1]}";
                    if (EscapeToChar.ContainsKey(s2))
                    {
                        sb.Append(EscapeToChar[s2]);
                        i++;
                    }
                    else
                    {
                        sb.Append(rmk[i]);
                    }
                }
                else
                {
                    sb.Append(rmk[i]);
                }
            }
            return sb.ToString();
        }

        public static string ProcessCharToEscape(string str)
        {
            var rmk = str;
            foreach (var esc in CharToEscape)
            {
                rmk = rmk.Replace(esc.Key.ToString(), esc.Value);
            }
            return $"\"{rmk}\"";
        }
    }

    public class Token
    {
        public string raw;
        public ASTNode? processedValue;
        public TokenType tokenType;
        public uint line;

        public Token(string raw, TokenType tokenType)
        {
            this.raw = raw;
            this.tokenType = tokenType;
            processedValue = null;
            line = 1;
        }

        public Token(ASTNode processedValue)
        {
            this.processedValue = processedValue;
            this.tokenType = TokenType.Processed;
            raw = string.Empty;
            line = 1;
        }

        public override string ToString()
        {
            return $"{tokenType}('{raw}') at line {line}";
        }
    }
}