using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using ScriptRuntime.Core;

namespace nejsc.Utils
{
    public class OutsideSymbolDetector
    {
        //闭包不会捕获的内置关键字
        static List<string> scanBypassKeyword = new List<string>() 
        {
            "this",
            "global",
            "null",
            "_closure",
        };
        public static List<string> DetectOutletSymbol(ASTNode root)
        {
            List<string> locals = new List<string>();
            List<string> outs = new List<string>();  
            Scan(root,locals,outs);
            return outs.ToHashSet().ToList();
        }

        static void Scan(ASTNode root,List<string> localSym,List<string> outletSym)
        {
            if(root.NodeType == ASTNode.ASTNodeType.Identifier)
            {
                //必须不在才能添加
                if (!localSym.Contains(root.Raw) && !scanBypassKeyword.Contains(root.Raw))
                {
                    outletSym.Add(root.Raw);
                }
            }
            else if(root.NodeType == ASTNode.ASTNodeType.VariableDefination)
            {
                localSym.Add(root.Raw);
            }
            else if(root.NodeType == ASTNode.ASTNodeType.MemberAccess)
            {
                //仅扫描左节点
                Scan(root.Childrens[0],localSym,outletSym);
            }
            else if(root.NodeType == ASTNode.ASTNodeType.TryCatchStatement)
            {
                Scan(root.Childrens[0], localSym, outletSym);
                //不扫描中间那个errObjectName，因为他是本地变量
                localSym.Add(root.Childrens[1].Raw);
                Scan(root.Childrens[2], localSym, outletSym);
            }
            else if(root.NodeType == ASTNode.ASTNodeType.FunctionDefinition)
            {
                //函数定义只扫描最后一个codeBlock，暂时将其参数视作本地变量
                for(int i = 0;i < root.Childrens.Count - 1; i++)
                {
                    localSym.Add(root.Childrens[i].Raw);
                }
                Scan(root.Childrens.Last(),localSym, outletSym);
            }
            else if(root.NodeType == ASTNode.ASTNodeType.Object)
            {
                foreach(var pair in root.Childrens)
                {
                    //扫描所有value部分，忽略key部分
                    Scan(pair.Childrens[1],localSym,outletSym);
                }
            }
            else
            {
                //其他照常处理

                foreach(var child in root.Childrens)
                {
                    Scan(child,localSym,outletSym);
                }
            }
        }
    }
}
