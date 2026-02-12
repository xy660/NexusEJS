# NexusEJS 快速指南

## 构建和部署

### 编写并编译脚本

1. 编写你的js脚本（熟悉JS的开发者快速入门请下滑看语法示例）
2. 运行nejsc编译器命令：`nejsc your_app.js`在目录下得到`your_app.nejs`和映射文件`your_app.nejs.map`

### 编译固件并部署

1. 选择你对应MCU版本的VM并编译PlatformIO项目，准备好SPIFFS文件系统
2. 将NexusEJS-Compiler（nejsc）编译得到的.nejs文件重命名为：`entry.nejs`并刷入文件系统
3. 刷写固件和文件系统，然后重启MCU

### 调试

1. **基础栈回溯行号获取**
复制异常对象中的stackTrace字段，使用offset2line工具运行命令：`offset2line your_app.nejs.map`加载map文件，然后根据提示粘贴栈回溯信息即可获得源码行号

2. **使用nejsdbg调试器**
使用如下命令启动nejsdbg调试器：
```bash
nejsdbg <mode> <addr>
```
举例：
```bash
nejsdbg serial COM3
```

然后你就能看到如下界面：

```bash
Connected COM3
Restart device to trig the default breakpoint.
====BreakPoint====
Stack trace:
  at main_entry() line:1
Scopes:
  {start:0, length:325, ep:325, attr:0}
  {start:6, length:319, ep:0, attr:0}
NexusEJS.Debugger>>
```

如果断点没自动触发，需要按下设备的RST按钮重启设备，启用调试的VM会自动在`main_entry`函数的第一行触发断点

### 尝试基础命令

在entry.js的30行打下断点（确保entry.nejs.map映射文件在同目录下，否则将会失败）
```bash
NexusEJS.Debugger>>break add entry.js:30
done
NexusEJS.Debugger>>break list
entry.js:30
entry.js:1
done
NexusEJS.Debugger>>break remove entry.js:30
done
NexusEJS.Debugger>>resume
done
NexusEJS.Debugger>>

```

手动暂停VM执行

```bash
NexusEJS.Debugger>>stw
at main_entry()  line:30

====BreakPoint====
Stack trace:
  at main_entry()  line:30
Variables:
  server = {"mapPost":<func>,"_id":1,"begin":<func>,"port":80,"_cb":[<clos_fn>],"finalize":<func>,"mapGet":<func>}
  cnt = 0
  obj = {"inc":<clos_fn>,"value":0}
Scopes:
  {start:0, length:325, ep:325, attr:0}
  {start:6, length:319, ep:319, attr:0}
  {start:275, length:50, ep:20, attr:1}
NexusEJS.Debugger>>
```

更多调试器示例请参阅NexusEJS-Debugger文档

---

NexusEJS语法和运行时为嵌入式进行优化，因此内存管理和编码风格和Web Javascript有所不同。

## 变量定义

NexusEJS的编译器禁用了`var`，你必须使用`let`定义变量 `const`将在未来支持

## 面向对象

NexusEJS不支持new关键字和class关键字，因此，你需要使用工厂模式（Object Factory）来对对象进行创建

```javascript

global.Human = {
    create(name,age){
        return {
            name:name,
            age:age,
            getInfo(){
                return "My name is " + this.name + " and my age is " + this.age;
            }
        }
    }
}

let man = Human.create("bob",25);
println(man.getInfo());

```

其中`global`表示全局变量空间，类似浏览器中的`window`，使用工厂模式可以避免prototype链的内存开销，因为MCU的KB级别内存非常宝贵

## 闭包

NexusEJS支持闭包，闭包将在以下情况触发：
- 作为参数调用
- 作为返回值
- 赋值

**请合理使用闭包，避免造成不必要内存占用，闭包仅会在lambda表达式或函数引用作用域外变量才会触发**

**被闭包捕获的基本类型会自动装箱为对象，将需要额外堆内存**

示例：

```javascript

function getCounter(){
    let cnt = 0;
    let obj = {value:0};
    return ()=>{
        cnt++;
        obj.value++;
    }
}

let inc = getCounter();
inc();

```

## 比较运算符

NexusEJS运行时行为默认类似传统Javascript的'use strict'，但是更加严格

NexusEJS不支持`"123"`和`123`混合使用，并且`==`运算符和标准JavaScript的`===`运算符行为一致，必须通过`Number.parseInt()`或`Number.parseFloat()`转换为同一类型

同理，数组索引也是不允许使用字符串的，避免隐式转换导致的未定义行为

## 类型转换

NexusEJS使用严格类型，`"123" != 123`，因此需要进行类型转换。
对于其他类型转字符串，不需要也不存在调用显式toString方法，NexusEJS在拼接字符串时会自动转字符串，例如`123 + "" == "123"`
对于字符串转数字，需要调用`Number.parseFloat(str : string)`，例如`Number.parseFloat("123") == 123`

数组不支持使用字符串表示的数字索引，不允许`❌arr["123"]`，这会直接抛出异常

## 内存管理

NexusEJS的内存管理使用自动垃圾清理（GC），当剩余内存百分比达到阈值自动触发，可以通过GC.h中的宏`LESS_MEMORY_TRIG_COLLECT`控制

也可以通过原生绑定函数：

```javascript
gc();
```

手动触发。GC触发时会触发世界暂停（Stop The World），需要自己权衡GC的使用，建议在需要高实时性的任务执行之前手动调用GC回收垃圾，避免造成以外的停顿

### READY TO FINALIZE机制

READY TO FINALIZE机制允许对象通过实现finalize() : boolean方法来控制自己的生命周期和释放绑定的原生资源

当GC即将回收对象的时候，finalize()方法会被GC调用，此时如果返回`false`则GC不会回收此对象及其所有子引用，如果返回`true`，则finalize()成员方法会被GC移除，下次GC时对象将被回收

**警告：请不要直接调用obj.finalize()，这个方法仅允许GC进行回调，否则会导致未定义行为**

## 多任务

NexusEJS的任务模型与标准Javascript不同，NexusEJS移除的事件循环，取而代之的是基于M:N架构的纤程调度器，一个Task（操作系统线程）调度若干VT（虚拟线程）

async和await关键字已被移除，但是可以通过`runTask(func,param) : TaskObject`方法创建任务，并获得一个`TaskObject`，可以通过：

- .id 获取任务id
- .isRunning() 获取任务是否正在运行
- .getResult() 获取任务返回值，如果任务还在运行则返回null

对于并发资源访问，需要使用上层OS提供的互斥锁临界区，当然NexusEJS提供了lock(obj){...}语法糖，示例：

```javascript

let sharedObject = {...};

//access in other task
lock(sharedObject){
    //operation safe here
}
```

如果是同一个线程内的两个虚拟线程之间的并发访问，请使用


```javascript

vtSetScheduleEnabled(true|false);

```

暂停虚拟线程调度器的抢占，仅对当前的物理线程/OS线程内的虚拟线程之间有效



Task默认栈大小是`8kb`并且在无OS环境不可用，因此可以使用虚拟线程：

```javascript
vtStart(()=>{...});
vtStart(myWork);
```
在当前线程调度虚拟线程，他们运行在同一个线程通过NexusVT调度器进行时间片轮转，因此不能使用delay或System.delay，可以通过`vtDelay`进行延迟操作
VT中进行长时间native层阻塞IO可能会导致其他VT无法得到CPU，因此阻塞IO请使用RTOS环境中的runTask
**请注意lock语句请勿用于两个同线程下的VT之间，否则将会造成死锁**

## Buffer类型

`Buffer`类型是NexusEJS专门设计的原生缓冲区绑定类，允许JavaScript脚本创建一个指定大小的原生缓冲区

使用示例：

```

let size = 32;
let buf = Buffer.create(size);
let offset = 0;
//writeUInt(offset,size,value)
buf.writeUInt(offset,4,123); //the size range 1-8, A 64-bit integer may overflow.
//readUInt(offset,size);
let read = buf.readUInt(offset,4);

//write string
buf.writeUTF16(offset,"hello");

buf = null; //disconnect the reference
gc(); //call gc to free the buffer

```














