# NexusEJS 快速指南

## 构建和部署

### 编写并编译脚本

1. 编写你的js脚本
2. 运行nejsc编译器命令：`nejsc your_app.js`在目录下得到`your_app.nejs`和映射文件`your_app.nejs.map`

### 编译固件并部署

1. 选择你对应MCU版本的VM并编译PlatformIO项目，准备好SPIFFS文件系统
2. 将NexusEJS-Compiler（nejsc）编译得到的.nejs文件重命名为：`entry.nejs`并刷入文件系统
3. 刷写固件和文件系统，然后重启MCU

### 调试

复制异常对象中的stackTrace字段，使用offset2line工具运行命令：`offset2line your_app.nejs.map`加载map文件，然后根据提示粘贴栈回溯信息即可获得源码行号

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

NexusEJS目前不支持闭包，也是为了内存而考虑，通常更推荐显式传递需要的信息，避免对象生命周期过长造成内存泄漏

示例：

```javascript

function getCounter(){
    let cnt = 0;
    return function inc(){
        cnt++;  //error: can't find symbol:cnt
    }
}

function getCounter(){
    let cnt = 0;
    return {
        innerCnt:cnt,
        inc(){
            this.innerCnt++; //worked
        }
    }
}

```

## 比较运算符

NexusEJS运行时行为默认类似传统Javascript的'use strict'，但是更加严格

NexusEJS不支持`"123"`和`123`混合使用，并且`==`运算符和标准JavaScript的`===`运算符行为一致，必须通过`Number.parse()`转换为同一类型

同理，数组索引也是不允许使用字符串的，避免隐式转换导致的未定义行为

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

## 多任务

NexusEJS的任务模型与标准Javascript不同，NexusEJS移除的事件循环，取而代之的是基于平台OS的真实线程，因此不必担心native方法阻塞其他任务，但是需要小心并发资源访问

async和await关键字已被移除，但是可以通过`runTask(func,param) : TaskObject`方法创建任务，并获得一个`TaskObject`，可以通过：

- .id 获取任务id
- .isRunning() 获取任务是否正在运行
- .getResult() 获取任务返回值，如果任务还在运行则返回null

对于并发资源访问，需要使用互斥锁临界区，当然NexusEJS提供了lock(obj){...}语法糖，示例：

```javascript

let sharedObject = {...};

//access in other task
lock(sharedObject){
    //operation safe here
}

```

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



