using System.IO.Ports;
using System.Text;
namespace nejsdbg
{
    internal class Program
    {
        static SerialPort port;

        static AutoResetEvent waitForDone = new AutoResetEvent(false);

        enum Mode
        {
            NONE,
            SERIAL,
            WIFI,
            BLE,
        }
        static Mode currMode;
        static string ReadNextMessage()
        {
            switch (currMode)
            {
                case Mode.NONE:
                    throw new Exception("invaild mode");
                case Mode.SERIAL:
                    {
                        while (true)
                        {
                            StringBuilder sb = new StringBuilder();
                            while (true)
                            {
                                char c = (char)port.ReadChar();
                                if(c == '\n')
                                {
                                    if (sb.ToString().StartsWith("debugger"))
                                    {
                                        return sb.ToString().Substring(9);
                                    }
                                    else
                                    {
                                        //Console.WriteLine($"stdout:{sb.ToString()}");
                                        sb.Clear();
                                    }
                                }
                                if (c != '\r' && c != '\0' && c != '\n')
                                {
                                    sb.Append(c);
                                }
                            }
                        }
                    }
            }
            return string.Empty;
        }
        static void SendMessage(string message)
        {
            switch (currMode)
            {
                case Mode.NONE:
                    throw new Exception("invaild mode");
                case Mode.SERIAL:
                    port.Write($"debugger:{message}\n");
                    break;
            }
        }
        class CurrentState
        {
            public int Offset;
            public int Line;
            public string PackageName = string.Empty;
            public List<string> StackTrace = new List<string>();
            public List<(string,string)> Variables = new();
            public List<string> Scopes = new List<string>();
            public List<string> VirtualStack = new List<string>();
            public bool Fresh = true;
        }

        static CurrentState curState = new CurrentState();
        static void DumpBasicInformation()
        {
            curState = new CurrentState();
            SendMessage("dump");
            SendMessage("stack");
        }

        static string _GetLineByRawFuncFrame(string funcFrameStr)
        {
            var sp = funcFrameStr.Split("offset:");
            string offset = sp.Last();
            string rawVal = funcFrameStr.Substring(0, funcFrameStr.Length - 7 - offset.Length);
            uint ioffset = uint.Parse(offset);
            string funcName = rawVal.Substring(3, rawVal.IndexOf("(") - 3);
            foreach (var package in LoadedSymbols)
            {
                foreach (var map in package.Value.mapper)
                {
                    if (map.Value.Item2 == funcName && ioffset <= map.Value.Item1)
                    {
                        return $"{sp[0]} line:{map.Key}";
                    }
                }
            }
            return funcFrameStr;
        }
        static void DisplayBreakPointInfo()
        {
            Console.WriteLine("\r\n====BreakPoint====");

            if (!string.IsNullOrEmpty(curState.PackageName))
            {
                Console.WriteLine($"Package: {curState.PackageName}");
            }

            if (curState.StackTrace.Count > 0)
            {
                Console.WriteLine("Stack trace:");
                foreach (var frame in curState.StackTrace)
                {
                    Console.WriteLine($"  {frame}");
                }
            }

            if (curState.Variables.Count > 0)
            {
                Console.WriteLine("Variables:");
                foreach (var (name, value) in curState.Variables)
                {
                    Console.WriteLine($"  {name} = {value}");
                }
            }

            if (curState.Scopes.Count > 0)
            {
                Console.WriteLine("Scopes:");
                foreach (var scope in curState.Scopes)
                {
                    Console.WriteLine($"  {scope}");
                }
            }

            if (curState.VirtualStack.Count > 0)
            {
                Console.WriteLine("Virtual stack:");
                foreach (var item in curState.VirtualStack)
                {
                    Console.WriteLine($"  {item}");
                }
            }

            if (curState.Fresh)
            {
                Console.WriteLine("(Fresh breakpoint)");
            }
        }
        static void ReadDebuggerMessageLoop()
        {
            while (true)
            {
                try
                {
                 
                    var recv = ReadNextMessage();
                    
                    if (!curState.Fresh && recv != "brk_trig" && !recv.StartsWith("brkp") && !recv.StartsWith("callstk:"))
                    {
                        if (recv.StartsWith("worker:"))
                        {
                            Console.WriteLine($"[worker]id={recv.Substring(7)}");
                        }
                        else
                        {
                            Console.WriteLine(recv);
                        }
                    }

                    if (recv == "done")
                    {
                        
                        if (curState.Fresh && curState.StackTrace.Count > 0)
                        {
                            curState.Fresh = false;
                            DisplayBreakPointInfo();
                        }
                        waitForDone.Set();

                        if(!curState.Fresh)
                            Console.Write("NexusEJS.Debugger>>");
                    }
                    else if(recv == "brk_trig") //断点触发
                    {
                        DumpBasicInformation();
                    }
                    else if (recv.StartsWith("var:"))
                    {
                        string pair = recv.Substring(4);
                        int tmp = pair.IndexOf("]");
                        string varName = pair.Substring(1, tmp - 1);
                        string content = pair.Substring(tmp + 1);
                        curState.Variables.Add((varName,content));
                    }
                    else if (recv.StartsWith("scp:"))
                    {
                        string scp = recv.Substring(4);
                        curState.Scopes.Add(scp);  
                    }
                    else if (recv.StartsWith("stk:"))
                    {
                        string val = recv.Substring(recv.IndexOf("]") + 1);
                        curState.VirtualStack.Add(val);
                    }
                    else if (recv.StartsWith("callstk:"))
                    {
                        //at funcName(...) offset:xxx

                        string callstk = _GetLineByRawFuncFrame(recv.Substring(8));

                        curState.StackTrace.Add(callstk);

                        Console.WriteLine(callstk);
                    }
                    else if(recv.StartsWith("brkp:"))
                    {
                        var sp = recv.Substring(5).Split(" ");
                        uint offset = uint.Parse(sp[2]);
                        uint line = 0;
                        foreach(var pck in LoadedSymbols)
                        {
                            if(pck.Value.Name == sp[0])
                            {
                                foreach(var map in pck.Value.mapper)
                                {
                                    if(map.Value.Item2 == sp[1] && map.Value.Item1 >= offset)
                                    {
                                        Console.WriteLine($"{pck.Value.Name}.js:{map.Key}");
                                        goto End_For;
                                    }
                                }
                            }
                        }
                        Console.WriteLine($"find_failed:{recv.Substring(5)}");
                    End_For:
                        continue;
                    }
                    

                }
                catch
                {

                }
            }
        }

        //通过文件解析行号和函数名称
        class PackageInfo
        {
            public string Name;
            public Dictionary<uint, (uint, string)> mapper = new Dictionary<uint, (uint, string)>();
        }
        static Dictionary<string, PackageInfo> LoadedSymbols = new Dictionary<string, PackageInfo>();
        static PackageInfo GetNejsMap(string fileName)
        {
            if (LoadedSymbols.ContainsKey(fileName))
            {
                return LoadedSymbols[fileName];
            }
            string rawPath = Path.Combine(Environment.CurrentDirectory, fileName);
            string mapPath;
            if (rawPath.EndsWith(".nejs.map"))
            {
                mapPath = rawPath;
            }
            else
            {
                mapPath = Path.GetFileNameWithoutExtension(rawPath) + ".nejs.map";
            }
            if (!File.Exists(mapPath))
            {
                throw new Exception($"the map file {mapPath} dos not exist");
            }
            string[] lines = File.ReadAllLines(mapPath);
            PackageInfo info = new PackageInfo();
            var firstLine = lines[0].Split(" ");
            //删掉末尾的后缀名
            info.Name = firstLine[0].Substring(0,firstLine[0].Length - 5);
            int funcCount = int.Parse(firstLine[1]);

            int pos = 1;
            while (pos < lines.Length)
            {
                var linesp = lines[pos].Split(" ");
                string funcName = linesp[0];
                int funcOffsetCount = int.Parse(linesp[1]);
                pos++;
                for (int i = 0; i < funcOffsetCount; i++)
                {
                    linesp = lines[pos].Split("|");
                    uint offset = uint.Parse(linesp[0]);
                    uint line = uint.Parse(linesp[1]);
                    
                    if(!info.mapper.ContainsKey(line))
                        info.mapper[line] = (offset, funcName);

                    pos++;
                }
            }
            LoadedSymbols[fileName] = info;
            return info;
        }

        static void HandCommand(string input)
        {
            var sp = input.Split(" ");
            if (sp[0] == "break" && sp[0].Length >= 2)
            {
                if (sp[1] == "add")
                {
                    var target = sp[2].Split(":");
                    var pck = GetNejsMap(target[0]);
                    var line = uint.Parse(target[1]);
                    var info = pck.mapper[line];
                    SendMessage($"brk {pck.Name} {info.Item2} {info.Item1}");
                }
                else if (sp[1] == "remove")
                {
                    var target = sp[2].Split(":");
                    var pck = GetNejsMap(target[0]);
                    var line = uint.Parse(target[1]);
                    var info = pck.mapper[line];
                    SendMessage($"brk_rm {pck.Name} {info.Item2} {info.Item1}");
                }
                else if (sp[1] == "list")
                {
                    SendMessage($"brk_ls");
                }
            }
            else
            {
                SendMessage(input);
            }

            waitForDone.WaitOne(10000);
        }
        static void HandledMessage()
        {
            foreach(var file in Directory.GetFiles(Environment.CurrentDirectory))
            {
                if (file.EndsWith(".nejs.map"))
                {
                    GetNejsMap(file);
                }
            }
            while (true)
            {
                //Console.Write("NexusEJS.Debugger>>");
                var input = Console.ReadLine();

                
                HandCommand(input);

                
            }
            
        }
        static void Main(string[] args)
        {
            if (args.Length == 0)
            {
                Console.WriteLine("Usage: nejsdbg <mode> <addr> <>\r\nExample: nejsdbg serial COM3");
                return;
            }
            if (args[0] == "serial")
            {
                port = new SerialPort();
                port.PortName = args[1];
                port.BaudRate = 9600;
                port.DataBits = 8;
                port.ReadTimeout = 5000;
                port.WriteTimeout = 5000;
                currMode = Mode.SERIAL;
                try
                {
                    port.Open();
                    Console.WriteLine("Connected " + args[1]);
                    Console.WriteLine("Restart device to trig the default breakpoint.");
                    Task.Run(() =>
                    {
                        ReadDebuggerMessageLoop();
                    });
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Open the port failed:" + ex.Message);
                }

                HandledMessage();

            }
            else if (args[0] == "wifi")
            {
                Console.WriteLine("no implement,waitting for next version.");
            }
            else
            {
                Console.WriteLine("unknown debug connection");
            }
        }
    }
}
