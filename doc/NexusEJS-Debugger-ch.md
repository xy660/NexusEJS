NexusEJS Debugger快速指南

#使用nejsdbg调试器

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

## 断点控制

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
```
stw命令将立即暂停整个VM的所有Worker运行，并且选定默认的主线程worker

示例：

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


断点触发后将自动展示局部变量和栈回溯

## 切换Worker

执行`workers`命令将展示当前的活跃worker

*此命令仅VM暂停状态下可用*

```bash
NexusEJS.Debugger>>workers
[worker]id=2
[worker]id=7
done
NexusEJS.Debugger>>
```

可以通过`set_wrk <worker_id>`命令切换worker

```bash
NexusEJS.Debugger>>set_wrk 2
done
NexusEJS.Debugger>>
```

随后可以通过`dump` `stack` 查看Worker的环境变量&作用域以及调用栈回溯

```bash
NexusEJS.Debugger>>dump
cmd:CALLFUNC
eip:295
var:[server]{"mapPost":<func>,"_id":1,"begin":<func>,"port":80,"_cb":[<clos_fn>],"finalize":<func>,"mapGet":<func>}
var:[cnt]0
var:[obj]{"inc":<clos_fn>,"value":0}
scp:{start:0, length:325, ep:325, attr:0}
scp:{start:6, length:319, ep:319, attr:0}
scp:{start:275, length:50, ep:20, attr:1}
done
NexusEJS.Debugger>>stack
at main_entry()  line:30
done
NexusEJS.Debugger>>
```

## 查看全局环境

使用`global`命令查看全局环境：

*此命令仅VM暂停状态下可用*

```bash
NexusEJS.Debugger>>global
var:[Http]{"createServer":<func>}
var:[WiFi]{"macAddress":<func>,"RSSI":<func>,"connect":<func>,"softAP":<func>,"isConnected":<func>,"setMode":<func>,"localIP":<func>,"SSID":<func>}
var:[global]{}
var:[Gpio]…
done
NexusEJS.Debugger>>
```
注意，这将会输出整个VM的所有全局符号及其内容，可能会非常庞大甚至有概率导致MCU序列化时内存溢出，只有在必要的时候才使用

## 单步运行

使用`step`命令进行单步运行

*此命令仅VM暂停状态下可用*

```bash
NexusEJS.Debugger>>step
done
NexusEJS.Debugger>>at main_entry()  line:2

====BreakPoint====
Stack trace:
  at main_entry()  line:2
Scopes:
  {start:0, length:325, ep:325, attr:0}
  {start:6, length:319, ep:3, attr:0}
Virtual stack:
  WiFi
NexusEJS.Debugger>>

```

此命令将会跳过本次断点，执行一条指令后再次断点


