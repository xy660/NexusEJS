# NexusEJS 标准API

## Buffer.*

- *Buffer.create(size: number)*  
  创建一个Buffer对象并开辟指定大小的原生内存空间

### Buffer实例方法

- *.readUInt(offset: number, size: number)*  
  从指定偏移量读取指定字节数的无符号整数

- *.readInt(offset: number, size: number)* 
  从指定偏移量读取指定字节数的有符号整数

- *.readFloat(offset: number)* 
  从指定偏移量读取4字节单精度浮点数

- *.readDouble(offset: number)*  
  从指定偏移量读取8字节双精度浮点数

- *.writeUInt(offset: number, size: number, value: number)*  
  向指定偏移量写入指定字节数的无符号整数

- *.writeInt(offset: number, size: number, value: number)*  
  向指定偏移量写入指定字节数的有符号整数

- *.writeFloat(offset: number, value: number)*  
  向指定偏移量写入4字节单精度浮点数

- *.writeDouble(offset: number, value: number)*  
  向指定偏移量写入8字节双精度浮点数

- *.readUTF8(offset: number, [maxBytes?: number])*  
  从指定偏移量读取UTF-8字符串，可选最大字节数限制

- *.writeUTF8(offset: number, str: string, [addNull?: boolean = true])*  
  向指定偏移量写入UTF-8字符串，可选是否添加空终止符

- *.readUTF16(offset: number, [length?: number])**  
  从指定偏移量读取UTF-16字符串，可选字符数限制

- *.writeUTF16(offset: number, str: string, [addNull?: boolean = true])*  
  向指定偏移量写入UTF-16字符串，可选是否添加空终止符

## Math.*

- *Math.sin(x: number)* - 正弦函数
- *Math.cos(x: number)* - 余弦函数
- *Math.tan(x: number)* - 正切函数
- *Math.asin(x: number)* - 反正弦函数
- *Math.acos(x: number)* - 反余弦函数
- *Math.atan(x: number)* - 反正切函数
- *Math.atan2(y: number, x: number)* - 四象限反正切
- *Math.sqrt(x: number)* - 平方根
- *Math.pow(x: number, y: number)* - 幂运算
- *Math.exp(x: number)* - 指数函数
- *Math.log(x: number)* - 自然对数
- *Math.log10(x: number)* - 常用对数
- *Math.abs(x: number)* - 绝对值
- *Math.ceil(x: number)* - 向上取整
- *Math.floor(x: number)* - 向下取整
- *Math.round(x: number)* - 四舍五入
- *Math.max(a: number, b: number)* - 最大值
- *Math.min(a: number, b: number)* - 最小值
- *Math.random()* - 返回[0,1)间的随机数
- *Math.degrees(rad: number)* - 弧度转角度
- *Math.radians(deg: number)* - 角度转弧度
- *Math.sinh(x: number)* - 双曲正弦
- *Math.cosh(x: number)* - 双曲余弦
- *Math.tanh(x: number)* - 双曲正切
- *Math.sign(x: number)* - 符号函数
- *Math.fmod(x: number, y: number)* - 浮点数取余
- *Math.trunc(x: number)* - 截断小数部分

### 数学常量

- *Math.PI* - 圆周率π
- *Math.E* - 自然常数e
- *Math.SQRT2* - 2的平方根
- *Math.SQRT1_2* - 1/2的平方根
- *Math.LN2* - 2的自然对数
- *Math.LN10* - 10的自然对数
- *Math.LOG2E* - 以2为底e的对数
- *Math.LOG10E* - 以10为底e的对数

## Number.*

- *Number.parseFloat(str: string)* - 解析字符串为浮点数
- *Number.parseInt(str: string)* - 解析字符串为整数
- *Number.toString(num: number)* - 数字转字符串

## 多线程与同步

- *runTask(entry: Function)* 
  启动新线程执行指定函数，返回任务控制对象

### 任务控制对象方法

- *.isRunning()* - 检查任务是否正在运行
- *.waitTimeout(timeout: number)* - 等待任务完成，指定超时时间
- *.getResult()* - 获取任务执行结果

### 同步与锁

语法糖：

``` javascript
lock(obj){
  //some operation
}
```

编译器自动转换为原始API：

```javascriot
try{
  mutexLock(obj);
  //some operation
  mutexUnlock(obj);
}catch(ex){
  mutexUnlock(obj);
}
```

- *mutexLock(obj: Object)* - 锁定对象的互斥锁
- *mutexUnlock(obj: Object)* - 解锁对象的互斥锁

### 基本调试IO

- *println(msg : string)*
使用默认波特率串口打印，并带有`print=>`前缀

### 系统控制

- *gc()*
强制在当前线程唤醒GC，暂停世界并回收内存垃圾

- *delay(ms : number)*

让出CPU使当前worker休眠指定毫秒数

## 动态模块

- *require(path : string) : any*
加载模块，同步执行模块的入口点并返回模块的return值
模块将会保持期声明周期直到代码中不持有模块的任何引用，模块将会被GC自动卸载

## 类型说明

- 所有Buffer读写方法的偏移量和大小参数必须满足：`0 ≤ offset < length` 且 `offset + size ≤ length`
- Buffer读写方法中的size参数限制在1-8字节之间
- 字符串读写方法支持UTF-8和UTF-16编码
- 数学函数对非数字参数返回NaN
- 多线程函数要求目标函数为无参本地函数
