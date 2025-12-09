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
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using ScriptRuntime.Core;
using ScriptRuntime.Runtime;

static class StringUtils
{
    
    public static Dictionary<char, char> bracket = new Dictionary<char, char>()
    {
        {'(',')'},
        {'[',']'},
        {'{','}'}
    };
    public static Dictionary<char, TokenType> bracketTokenType = new Dictionary<char, TokenType>()
    {
        {'(',TokenType.Part},
        {'[',TokenType.IndexLabel},
        {'{',TokenType.CodeBlock}
    };
    public static string FindNextToken(string s, int index) //单行语句搜寻
    {
        Stack<char> bracketStack = new Stack<char>(); //括号平衡栈
        bool inString = false;
        for (int i = index; i < s.Length; i++)
        {
            if (s[i] == '"')
            {
                inString = !inString;
            }
            else if (bracket.ContainsKey(s[i]) && !inString)
            {
                bracketStack.Push(s[i]);
            }
            else if (bracket.ContainsValue(s[i]) && !inString)
            {
                if (bracket[bracketStack.Pop()] != s[i])
                {
                    throw new SyntaxException("解析语法错误：括号错误",s);
                }
            }
            if (bracketStack.Count == 0 && s[i] == ';' && !inString) //寻找到分号表示token末尾
            {
                return s.Substring(index, i - index + 1);
            }
        }
        return string.Empty;
    }
    public static string FindNextWord(string s, int index) //单个单词搜寻
    {
        Stack<char> bracketStack = new Stack<char>(); //括号平衡栈
        for (int i = index; i < s.Length; i++)
        {
            if (bracket.ContainsKey(s[i]))
            {
                bracketStack.Push(s[i]);
            }
            else if (bracket.ContainsValue(s[i]))
            {
                if (bracket[bracketStack.Pop()] != s[i])
                {
                    throw new SyntaxException("解析语法错误：括号错误",s);
                }
            }
            if (bracketStack.Count == 0 && !char.IsLetterOrDigit(s[i])) //寻找到分号表示token末尾
            {
                return s.Substring(index, i - index + 1);
            }
        }
        return string.Empty;
    }
    public static ValueTuple<string, int> FindNextCharacter(string s, int index, string target)
    {
        for (int i = index; i <= s.Length - target.Length; i++)
        {
            if (s.Substring(i, target.Length) == target)
            {
                return (s.Substring(index, i - index), i);
            }
        }
        return (string.Empty, -1);
    }
    public static string NextToken(string s, ref int index) 
    {
        for(int i = index; i < s.Length; i++)
        {
            if(!char.IsLetterOrDigit(s[i]))
            {
                var ret = s.Substring(index, i - index);
                index = i;
                return ret;
            }
        }
        return string.Empty;
    }
    public static List<string> SplitArgSyntax(string raw)
    {
        Stack<char> bracketStack = new Stack<char>(); //括号平衡栈
        bool inString = false;
        StringBuilder sb = new StringBuilder();
        var ret = new List<string>();
        for (int i = 0; i < raw.Length; i++)
        {
            if (raw[i] == '"')
            {
                inString = !inString;
            }
            else if (bracket.ContainsKey(raw[i]) && !inString)
            {
                bracketStack.Push(raw[i]);
            }
            else if (bracket.ContainsValue(raw[i]) && !inString)
            {
                if (bracket[bracketStack.Pop()] != raw[i])
                {
                    throw new SyntaxException("解析语法错误：括号错误",raw);
                }
            }
            if (bracketStack.Count == 0 && raw[i] == ',' && !inString) //寻找到末尾
            {
                ret.Add(sb.ToString());
                sb.Clear();
            }
            else
            {
                //if(inString || (!inString && raw[i] != ' '))
                if (true)
                {
                    sb.Append(raw[i]);
                }
            }
        }
        if (sb.Length > 0)
            ret.Add(sb.ToString());

        return ret;
    }


    public static string ClearMultiSpace(string s)
    {
        if (string.IsNullOrEmpty(s))
            return s;

        // 去除开头和结尾的空格
        s = s.Trim();

        StringBuilder sb = new StringBuilder();
        bool inString = false;
        bool hasSpace = false;

        for (int i = 0; i < s.Length; i++)
        {
            char c = s[i];

            // 检查是否进入或退出字符串
            if (c == '"' && (i == 0 || s[i - 1] != '\\'))
            {
                inString = !inString;
            }

            if (inString)
            {
                // 字符串内内容原样保留
                sb.Append(c);
            }
            else
            {
                if (c == ' ')
                {
                    // 非字符串内的空格，检查是否已有空格
                    if (!hasSpace)
                    {
                        sb.Append(c);
                        hasSpace = true;
                    }
                }
                else
                {
                    hasSpace = false;
                    sb.Append(c);
                }
            }
        }

        return sb.ToString();
    }


    public static string RemoveSingleLineComments(string code)
    {
        if (string.IsNullOrEmpty(code))
            return code;

        StringBuilder result = new StringBuilder();
        StringBuilder currentLine = new StringBuilder();
        bool inString = false;      // 是否在字符串中
        bool inChar = false;        // 是否在字符字面量中
        bool inVerbatimString = false; // 是否在逐字字符串中
        bool escapeNext = false;    // 下一个字符是否需要转义
        int commentStartIndex = -1; // 注释开始位置

        for (int i = 0; i < code.Length; i++)
        {
            char current = code[i];
            char next = i < code.Length - 1 ? code[i + 1] : '\0';

            // 处理转义字符
            if (escapeNext)
            {
                currentLine.Append(current);
                escapeNext = false;
                continue;
            }

            // 处理字符串和字符字面量
            if (inString || inChar || inVerbatimString)
            {
                currentLine.Append(current);

                if (inString && current == '\\')
                {
                    escapeNext = true;
                }
                else if (inChar && current == '\\')
                {
                    escapeNext = true;
                }
                else if (inString && current == '"')
                {
                    inString = false;
                }
                else if (inChar && current == '\'')
                {
                    inChar = false;
                }
                else if (inVerbatimString && current == '"')
                {
                    // 逐字字符串中两个连续引号表示一个引号字符
                    if (next == '"')
                    {
                        currentLine.Append(next);
                        i++; // 跳过下一个引号
                    }
                    else
                    {
                        inVerbatimString = false;
                    }
                }
                continue;
            }

            // 检查是否进入字符串/字符字面量
            if (current == '"')
            {
                if (i > 0 && code[i - 1] == '@')
                {
                    inVerbatimString = true;
                    currentLine.Append(current);
                }
                else
                {
                    inString = true;
                    currentLine.Append(current);
                }
                continue;
            }
            else if (current == '\'')
            {
                inChar = true;
                currentLine.Append(current);
                continue;
            }

            // 检查注释开始
            if (current == '/' && next == '/' && commentStartIndex == -1)
            {
                commentStartIndex = i;
                // 跳过注释的第二个斜杠
                i++;
                continue;
            }

            // 如果不在注释中，添加字符到当前行
            if (commentStartIndex == -1)
            {
                currentLine.Append(current);
            }

            // 检查行结束（换行符）
            if (current == '\r' || current == '\n')
            {
                // 如果这一行有注释，只添加注释之前的内容
                if (commentStartIndex != -1)
                {
                    // 注释开始前的内容已经添加到currentLine中
                    result.Append(currentLine.ToString());
                    commentStartIndex = -1;
                }
                else
                {
                    result.Append(currentLine.ToString());
                }

                // 添加换行符本身
                result.Append(current);

                // 处理Windows风格的换行符 \r\n
                if (current == '\r' && next == '\n')
                {
                    result.Append(next);
                    i++;
                }

                currentLine.Clear();
            }
        }

        // 处理最后一行（可能没有换行符）
        if (commentStartIndex != -1)
        {
            // 最后一行有注释，只添加注释之前的内容
            result.Append(currentLine.ToString());
        }
        else
        {
            result.Append(currentLine.ToString());
        }

        return result.ToString();
    }
}

