using System;
using System.Collections.Generic;
using System.IO;
using System.Text.RegularExpressions;

namespace StackTraceMapper
{
    public class SymbolMapper
    {
        private class MethodSymbol
        {
            public string MethodName { get; set; }
            public Dictionary<int, int> OffsetToLine { get; } = new(); // offset -> line
        }

        private string currentFileName = "";
        private readonly Dictionary<string, MethodSymbol> methodCache = new();

        public void LoadSymbolFile(string filePath)
        {
            var lines = File.ReadAllLines(filePath);
            if (lines.Length == 0) return;

            // Parse first line: filename method_count
            var firstLine = lines[0].Trim();
            var firstParts = firstLine.Split(' ', StringSplitOptions.RemoveEmptyEntries);
            if (firstParts.Length < 2) return;

            currentFileName = firstParts[0];
            if (!int.TryParse(firstParts[1], out int methodCount)) return;

            int currentLineIndex = 1;

            for (int i = 0; i < methodCount && currentLineIndex < lines.Length; i++)
            {
                // Parse method header: method_name kv_count
                var methodHeader = lines[currentLineIndex].Trim();
                currentLineIndex++;

                var headerParts = methodHeader.Split(' ', StringSplitOptions.RemoveEmptyEntries);
                if (headerParts.Length < 2) continue;

                string methodName = headerParts[0];
                if (!int.TryParse(headerParts[1], out int kvCount)) continue;

                var methodSymbol = new MethodSymbol { MethodName = methodName };

                // Parse offset|line pairs
                for (int j = 0; j < kvCount && currentLineIndex < lines.Length; j++)
                {
                    var kvLine = lines[currentLineIndex].Trim();
                    currentLineIndex++;

                    var kvParts = kvLine.Split('|');
                    if (kvParts.Length != 2) continue;

                    if (int.TryParse(kvParts[0], out int offset) &&
                        int.TryParse(kvParts[1], out int line))
                    {
                        methodSymbol.OffsetToLine[offset] = line;
                    }
                }

                methodCache[methodName] = methodSymbol;
            }
        }

        public (string fileName, int line) FindSourceLocation(string methodName, int offset)
        {
            if (string.IsNullOrEmpty(currentFileName))
            {
                return ("unknown", 0);
            }

            if (!methodCache.TryGetValue(methodName, out var methodSymbol))
            {
                return ("unknown", 0);
            }

            // Find the last mapping with offset <= given offset
            int bestLine = 0;
            int bestOffset = -1;

            foreach (var kvp in methodSymbol.OffsetToLine)
            {
                if (kvp.Key <= offset && kvp.Key > bestOffset)
                {
                    bestOffset = kvp.Key;
                    bestLine = kvp.Value;
                }
            }

            if (bestOffset >= 0)
            {
                return (currentFileName, bestLine);
            }

            return (currentFileName, 0);
        }

        public string ConvertStackTrace(string stackTrace)
        {
            var lines = stackTrace.Split(new[] { '\n', '\r' }, StringSplitOptions.RemoveEmptyEntries);
            var result = new List<string>();

            foreach (var line in lines)
            {
                var originalLine = line.Trim();
                if (string.IsNullOrEmpty(originalLine))
                {
                    result.Add("");
                    continue;
                }

                // Find "offset:" position
                int offsetIndex = originalLine.IndexOf("offset:", StringComparison.Ordinal);
                if (offsetIndex == -1)
                {
                    result.Add(originalLine);
                    continue;
                }

                // Extract offset value
                string offsetPart = originalLine.Substring(offsetIndex + 7).Trim();
                if (!int.TryParse(offsetPart, out int offset))
                {
                    result.Add(originalLine);
                    continue;
                }

                // Extract part before "offset:"
                string beforeOffset = originalLine.Substring(0, offsetIndex).Trim();
                if (!beforeOffset.StartsWith("at "))
                {
                    result.Add(originalLine);
                    continue;
                }

                // Extract method name and parameters
                string methodPart = beforeOffset.Substring(3).Trim(); // Remove "at "

                // Separate method name and parameters
                string methodName;
                string parameters = "()";

                int parenIndex = methodPart.IndexOf('(');
                if (parenIndex > 0)
                {
                    // Has parameters
                    methodName = methodPart.Substring(0, parenIndex).Trim();
                    int closeParenIndex = methodPart.LastIndexOf(')');
                    if (closeParenIndex > parenIndex)
                    {
                        parameters = methodPart.Substring(parenIndex, closeParenIndex - parenIndex + 1);
                    }
                }
                else
                {
                    // No parameters
                    methodName = methodPart;
                }

                // Find source location
                var (fileName, lineNum) = FindSourceLocation(methodName, offset);

                if (lineNum > 0)
                {
                    result.Add($"at {methodName}{parameters} file:{fileName} line:{lineNum}");
                }
                else
                {
                    result.Add($"at {methodName}{parameters} file:unknown line:0");
                }
            }

            return string.Join(Environment.NewLine, result);
        }
    }

    public class Program
    {
        public static void Main(string[] args)
        {
            if (args.Length < 1)
            {
                Console.WriteLine("Usage: StackMapper.exe <symbol_file>");
                Console.WriteLine("Example: StackMapper.exe test.nejs.map");
                Console.WriteLine();
                Console.WriteLine("Then paste stack trace (Ctrl+Z to finish)");
                return;
            }

            string symbolFile = args[0];

            try
            {
                var mapper = new SymbolMapper();

                Console.WriteLine($"Loading symbol file: {symbolFile}");

                if (!File.Exists(symbolFile))
                {
                    Console.WriteLine($"Error: File '{symbolFile}' not found!");
                    return;
                }

                mapper.LoadSymbolFile(symbolFile);
                Console.WriteLine("Symbol file loaded successfully!");
                Console.WriteLine();

                Console.WriteLine("Paste stack trace (multiple lines supported, Ctrl+Z then Enter to finish):");
                Console.WriteLine("--------------------------------------------------------");

                // Read multiple lines of input
                var inputLines = new List<string>();
                string line;
                while ((line = Console.ReadLine()) != null)
                {
                    inputLines.Add(line);
                }

                string stackTrace = string.Join(Environment.NewLine, inputLines);

                if (string.IsNullOrWhiteSpace(stackTrace))
                {
                    Console.WriteLine("No stack trace input provided!");
                    return;
                }

                Console.WriteLine("\nConverting stack trace...");
                string converted = mapper.ConvertStackTrace(stackTrace);

                Console.WriteLine("\nConversion Result:");
                Console.WriteLine("--------------------------------------------------------");
                Console.WriteLine(converted);
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error: {ex.Message}");
                Console.WriteLine($"Stack Trace: {ex.StackTrace}");
            }

            Console.WriteLine("\nPress any key to exit...");
            Console.ReadKey();
        }
    }
}