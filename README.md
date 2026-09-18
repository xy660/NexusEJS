[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
![Language](https://img.shields.io/badge/Embedded%20JS-Engine-orange?logo=javascript)

![ESP32](https://img.shields.io/badge/Now%20Support-ESP32-red?logo=espressif)

# NexusEJS

### 低内存，低资源的嵌入式JavaScript引擎

## 设计目标

NexusEJS目标是降低IoT的开发门槛，让更多人也能参与IoT设备的开发。NexusEJS使用JavaScript风格语法，并针对嵌入式进行了优化和功能改进。

您只需要一行代码即可点亮`ESP32`或任何具备支持驱动的开发SoC的led灯：

```javascript
Gpio.set(2,true);
```

轻松实现多任务：

```javascript
vtStart(processData);
vtStart(readButton); //在当前线程内调度虚拟线程（裸机可用）
runTask(readSensor); //启动RTOS线程（需要操作系统支持）
```

NexusEJS在ECMA标准以外设计了：

- 析构器保活机制（析构器可拒绝GC回收自身）
- 并发原语与并发安全语法（采用M:N架构，支持虚拟线程&RTOS绑定层）
- 支持模块感知GC，自动卸载无用字节码缓存和常量池缓存，自动托管require.cache不暴露给脚本层
- 支持手动控制GC时机
- 可定制和裁剪的VM功能
- 平台无关且易于移植的VM核心
- 便捷的C/C++原生绑定，仅需一个C++lambda即可完成函数绑定

在ESP32上的测试中，单任务，纯数值计算任务的情况下：
- 平均内存占用约`55KB`
- 裁剪后的固件大小约`0.5MB`
- 纯栈分配小对象可无GC暂停

在对象有对象创建但不频繁的场景，通过手动提前触发GC也可降低停顿频率和时间

## 快速入门

1. 下载并编译NexusEJS-Toolchain 
2. 根据你的MCU选择对应的驱动例如`ESP32Driver.h` 将需要的功能对应的宏的值改为`1`
3. 编写你的js代码
4. 运行`nejsc your_app.js` 得到`your_app.nejs`
5. 将其放入SPIFFS的data文件中并命名为`entry.nejs` 然后上传FS Image和Firmware

**你可以在`/demo`文件夹找到示例，在`/doc`文件夹找到开发文档及其api文档**

## 如何贡献

欢迎为 NexusEJS 项目做出贡献！无论是报告 bug、建议新功能、改进文档，还是提交代码更改，我们都非常感谢您的帮助。

### 报告问题
如果您遇到任何 bug、有疑问，或想建议新功能，请在 GitHub 仓库中创建一个 issue。

感谢您帮助改进 NexusEJS！


