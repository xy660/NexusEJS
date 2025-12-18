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

namespace ScriptRuntime.Runtime
{
    //脚本异常基类
    public class ScriptBaseException : Exception
    {
        public object Tag = 0;
        public ScriptBaseException(string message) : base(message)
        {

        }
    }
    
    //脚本语法解析过程中的异常
    public class SyntaxException : ScriptBaseException
    {
        public object Tag = 0;
        public SyntaxException(string message,Token token) : base(message + "  line:" + token.line)
        {

        }
        public SyntaxException(string message, string tag) : base(message + "  Context:" + tag)
        {

        }
    }

}