[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
![Language](https://img.shields.io/badge/Embedded%20JS-Engine-orange?logo=javascript)

![ESP32](https://img.shields.io/badge/Now%20Support-ESP32-red?logo=espressif)
[![STM32](https://img.shields.io/badge/🔜%20Future%20Roadmap-STM32%20Series-03234B?logo=stmicroelectronics)]()

[![GC Mark-Sweep](https://img.shields.io/badge/🧹%20GC%20-Support-success)]()
[![Closures](https://img.shields.io/badge/🔗%20Closures%20-Support-success)]()
[![Memory](https://img.shields.io/badge/💾%20Memory-~55KB-brightgreen)]()

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

## How to Contribute

We welcome contributions to the NexusEJS project! Whether it's reporting bugs, suggesting features, improving documentation, or submitting code changes, your help is greatly appreciated.

### Reporting Issues
If you encounter any bugs, have questions, or would like to suggest new features, please open an issue on our GitHub repository.

**Issue Tracker**: https://github.com/xy660/NexusEJS/issues

### Submitting Code Changes
To contribute code changes:
1. Fork the repository on GitHub.
2. Create a new branch for your changes.
3. Make your modifications and ensure they follow the project's coding style.
4. Test your changes thoroughly.
5. Submit a pull request to the main repository.

**Repository**: https://github.com/xy660/NexusEJS  
**Pull Requests**: https://github.com/xy660/NexusEJS/pulls

### Contribution Guidelines
- Follow the existing code style and conventions.
- Ensure your changes are well-documented.
- Include tests for new functionality when applicable.
- Keep pull requests focused and atomic.

Thank you for helping improve NexusEJS!


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

## 如何贡献

欢迎为 NexusEJS 项目做出贡献！无论是报告 bug、建议新功能、改进文档，还是提交代码更改，我们都非常感谢您的帮助。

### 报告问题
如果您遇到任何 bug、有疑问，或想建议新功能，请在 GitHub 仓库中创建一个 issue。

**问题跟踪**: https://github.com/xy660/NexusEJS/issues

### 提交代码更改
要贡献代码更改：
1. 在 GitHub 上 Fork 本仓库。
2. 为您的更改创建一个新分支。
3. 进行修改，并确保它们符合项目的编码风格。
4. 彻底测试您的更改。
5. 向主仓库提交 pull request。

**仓库地址**: https://github.com/xy660/NexusEJS  
**拉取请求**: https://github.com/xy660/NexusEJS/pulls

### 贡献指南
- 遵循现有的代码风格和规范。
- 确保您的更改有良好的文档说明。
- 在适用情况下，为新功能包含测试。
- 保持 pull request 的专注和原子性。

感谢您帮助改进 NexusEJS！


