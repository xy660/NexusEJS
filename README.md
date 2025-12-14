
[中文README](#readme-cn)

# NexusEJS

An embedded JavaScript engine that supports native RTOS multitasking and automatic memory management.

## Design Goals

NexusEJS aims to lower the development barrier for IoT, enabling more people to participate in IoT device development. It uses a familiar JavaScript-style syntax, optimized and enhanced for embedded systems.

You can turn on an LED on an **ESP32** or any supported SoC with just one line of code:

```javascript
Gpio.set(2, true);
```

NexusEJS emphasizes determinism and transparency. Our VM is designed with:

- **READY TO FINALIZE** object lazy reclamation mechanism
- Concurrency primitives and concurrency-safe syntax
- Support for manual GC timing control
- Customizable and trimmable VM features
- Platform-agnostic, easily portable VM core
- Native C/C++ binding

In tests on ESP32, under a single task and without frequent object creation:
- Average memory usage: about **55 KB**
- Trimmed firmware size: about **0.5 MB**
- No GC pauses

## Quick Start

1. Download and compile the NexusEJS Toolchain.
2. Select the appropriate driver for your MCU (e.g., `ESP32Driver.h`) and set the macros for required features to `1`.
3. Write your JavaScript code.
4. Run `nejsc your_app.js` to generate `your_app.nejs`.
5. Place it in the SPIFFS data folder named `entry.nejs`, then upload the FS Image and Firmware.


---

<a id="readme-cn"></a>

# NexusEJS

支持原生RTOS多任务和自动内存管理的嵌入式JavaScript引擎

## 设计目标

NexusEJS致力于降低IoT的开发门槛，让更多人也能参与IoT设备的开发。NexusEJS使用熟悉的JavaScript风格语法，并针对嵌入式进行了优化和功能改进。

您只需要一行代码即可点亮`ESP32`或任何具备支持驱动的开发SoC的led灯：

```javascript
Gpio.set(2,true);
```

NexusEJS追求确定性和透明性，因此我们的VM设计了：

- READY TO FINALIZE对象延迟回收机制
- 并发原语与并发安全语法
- 支持手动控制GC时机
- 可定制和裁剪的VM功能
- 平台无关且易于移植的VM核心
- C/C++原生绑定

在ESP32上的测试中，单任务，无频繁对象创建的情况下：
- 平均内存占用约`55KB`
- 裁剪后的固件大小约`0.5MB`
- 无GC暂停

在对象有对象创建但不频繁的场景，通过手动提前触发GC也可降低停顿频率和时间

## 快速入门

1. 下载并编译NexusEJS-Toolchain 
2. 根据你的MCU选择对应的驱动例如`ESP32Driver.h` 将需要的功能对应的宏的值改为`1`
3. 编写你的js代码
4. 运行`nejsc your_app.js` 得到`your_app.nejs`
5. 将其放入SPIFFS的data文件中并命名为`entry.nejs` 然后上传FS Image和Firmware

