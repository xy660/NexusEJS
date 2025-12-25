# NexusEJS代码模块 规范和使用

## 模块化示例

NexusEJS的`require`函数提供类似npm风格的模块化支持，但其本质是加载给定路径的nejs文件，并同步执行入口点，然后返回入口点的返回值

因此不使用export关键字进行导出，直接使用`return`即可

示例：

***entry.js***

```javascript
let pck = require("/test.nejs");
println("package export:" + pck);
println("test export:" + pck.getInnerMessage());
println("global=" + global);
println("test global export function: ");
testFunc();
pck = null;
gc(); //解除模块引用，此时触发GC模块将被自动卸载
return "success";
```

***test.js***

```javascript
println("the package loaded!!");
delay(2000);
let innerMessage = "hello world!"; //private variable
global.testFunc = ()=>println("testFunc!! inner=" + innerMessage); //global export
//local export
return {
    message:"from other package",
    getInnerMessage(){
        return innerMessage;
    }
};
```
示例中，如果需要作为require的模块对象返回值，则直接使用return返回，如果需要注册符号到全局，则直接增加global对象关键字（需要注意符号冲突）

### V1.3.1版本后模块将在不存在任何引用后由GC自动卸载，释放内存




