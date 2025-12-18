# NexusEJS  ESP32平台API文档

## GPIO类

### `Gpio.set(pin : number,val : true)`

向指定引脚写入数字信号量

### `Gpio.read(pin : number) : boolean`

读取指定引脚的数字信号量

### `Gpio.readAnalog(pin : number) : number`

读取指定引脚的模拟信号量，默认10bit精度，返回归一化数据（0-1）

### `Gpio.readAnalogRaw(pin : number) : number`

读取指定引脚的模拟信号量，默认10bit精度，返回原始数据（对于10bit精度返回0-1023）

### `Gpio.pwmChannels : PWM[]`

一组常量，表示平台硬件支持的GPIO通道实例，通过`Gpio.pwmChannels.length`可以获取实际通道数

### `PWM.channel : number`

表示当前PWM通道实例的通道id

### `PWM.frequency : number`

表示当前PWM通道的频率

### `PWM.resolution : number`

表示当前PWM通道的分辨率，例如10bit分辨率则`PWM.resolution`为10

### `PWM.attach(pin : number)`

绑定此通道到引脚，并更新参数


### `PWM.setDuty(duty : number)`

设置通道占空比，10bit下范围为0-1023


---

## I2C类

### `I2C.init(sdaPin: number, sclPin: number, freq: number): I2CInstance`

初始化I2C总线，返回I2C实例对象

## I2CInstance接口

I2C实例对象，用于操作I2C总线

### 属性
- **`sda: number`** - 只读属性，返回SDA引脚号
- **`scl: number`** - 只读属性，返回SCL引脚号
- **`freq: number`** - 只读属性，返回频率(Hz)

### `I2CInstance.write(addr: number, data: number | Buffer | number[]): boolean`

写入数据到I2C设备
- **`addr`** - 设备地址(0x00-0x7F)
- **`data`** - 要写入的数据(数字、Buffer或数组)
- **返回值** - 是否成功

### `I2CInstance.read(addr: number, length: number): number[]`

从I2C设备读取数据
- **`addr`** - 设备地址(0x00-0x7F)
- **`length`** - 要读取的字节数(1-1024)
- **返回值** - 读取到的数据数组

### `I2CInstance.writeReg(addr: number, reg: number, data: number | Buffer | number[]): boolean`

写入寄存器值
- **`addr`** - 设备地址
- **`reg`** - 寄存器地址
- **`data`** - 要写入的数据(数字、Buffer或数组)
- **返回值** - 是否成功

### `I2CInstance.readReg(addr: number, reg: number, length: number): number[]`

读取寄存器值
- **`addr`** - 设备地址
- **`reg`** - 寄存器地址
- **`length`** - 要读取的字节数(1-1024)
- **返回值** - 读取到的数据数组

### `I2CInstance.scan(): number[]`

扫描I2C总线上的设备
- **返回值** - 所有发现的设备地址数组

### `I2CInstance.close(): boolean`

关闭I2C连接，释放资源
- **返回值** - 是否成功

---

## WiFi 类

### 连接管理

#### `WiFi.softAP(ssid : string, password? : string) : boolean`
开启 WiFi 热点

#### `WiFi.connect(ssid : string, password? : string) : boolean`
连接到 WiFi 网络

### 状态查询

#### `WiFi.isConnected() : boolean`
返回当前连接状态

#### `WiFi.localIP() : string`
返回本地 IP 地址

#### `WiFi.SSID() : string`
返回当前连接的网络名称

#### `WiFi.RSSI() : number`
返回信号强度（RSSI 值）

#### `WiFi.macAddress() : string`
返回设备 MAC 地址

### 模式配置

#### `WiFi.setMode(mode : string) : void`
设置 WiFi 工作模式
- **模式参数**:
  - `"ap"`: 仅接入点模式
  - `"sta"`: 仅站点模式
  - `"ap_sta"`: 混合模式
  - `"none"`: 禁用 WiFi
 
---

## Socket 类

### TCP 连接

#### `Socket.connectTCP(ip : string, port : number) : SocketObject`
建立 TCP 连接到指定 IP 和端口
- **参数**:
  - `ip`: IPv4 地址字符串 (如 "192.168.1.100")
  - `port`: 端口号
- **返回**: SocketObject 实例
- **异常**: 无效 IP 格式或连接失败时抛出错误

### SocketObject 实例方法

#### `socket.send(buffer : Buffer, offset : number, length : number) : void`
向连接发送缓冲区数据
- **参数**:
  - `buffer`: Buffer 对象
  - `offset`: 起始偏移量
  - `length`: 发送字节数
- **异常**: 连接已关闭、Buffer 无效或偏移超出范围时抛出错误

#### `socket.recv(buffer : Buffer, offset : number, length : number) : number`
从连接接收数据到缓冲区
- **参数**:
  - `buffer`: Buffer 对象
  - `offset`: 起始偏移量
  - `length`: 最大接收字节数
- **返回**: 实际接收的字节数
- **异常**: 连接已关闭、Buffer 无效或偏移超出范围时抛出错误

#### `socket.recvSize(buffer : Buffer, offset : number, length : number) : boolean`
精确接收指定长度的数据（阻塞直到收到指定长度）
- **参数**:
  - `buffer`: Buffer 对象
  - `offset`: 起始偏移量
  - `length`: 要接收的字节数
- **返回**: 是否成功接收指定长度
- **异常**: 连接已关闭、Buffer 无效或偏移超出范围时抛出错误

#### `socket.isConnected() : boolean`
检查连接是否活动
- **返回**: 连接状态

#### `socket.close() : boolean`
关闭连接
- **返回**: 操作是否成功

#### `socket.setTimeout(timeout : number) : boolean`
设置操作超时（毫秒）

---

## Http类

### HTTP客户端

### `Http.fetch(url : string, options? : object) : Response`
执行 HTTP 请求
- **参数**:
  - `url`: 请求的 URL
  - `options`: 可选配置对象
    - `method`: 请求方法，支持 "GET"、"POST"、"PUT"、"DELETE"（默认: "GET"）
    - `body`: 请求体（POST/PUT 时有效）
- **返回**: Response 对象
- **异常**: URL 无效或 HTTP 客户端初始化失败时抛出错误
- **注意**: 此函数是同步的，会阻塞直到请求完成

### Response 对象

#### 属性
- `ok` : boolean - 请求是否成功（状态码 2xx）
- `status` : number - HTTP 状态码
- `statusText` : string - 状态描述文本
- `url` : string - 原始请求 URL

#### 方法
- `response.text() : string` - 返回响应文本
- `response.buffer()` - 当前版本未实现，调用会抛出错误
- **参数**:
  - `timeout`: 超时时间（毫秒）
- **返回**: 设置是否成功
- **异常**: 参数类型错误时抛出错误

## HTTP 服务器

### `Http.createServer(port : number) : ServerObject`
创建 HTTP 服务器实例
- **参数**:
  - `port`: 服务器监听的端口号
- **返回**: ServerObject 服务器实例
- **异常**: 端口参数非数字时抛出错误

### ServerObject 实例方法

#### `server.begin() : void`
启动 HTTP 服务器
- **注意**: 会创建独立的 RTOS 任务处理请求
- **异常**: 启动失败时抛出错误

#### `server.mapGet(url : string, callback : function) : void`
注册 GET 请求处理器
- **参数**:
  - `url`: 请求路径
  - `callback`: 回调函数，接收一个对象参数，包含请求参数
- **回调参数对象**: 包含请求参数键值对
- **返回值**: 回调函数的返回值会作为 HTTP 响应发送
- **异常**: 参数类型错误时抛出错误

#### `server.mapPost(url : string, callback : function) : void`
注册 POST 请求处理器
- **参数**:
  - `url`: 请求路径
  - `callback`: 回调函数，接收一个对象参数，包含请求参数
- **回调参数对象**: 包含 POST 请求参数键值对
- **返回值**: 回调函数的返回值会作为 HTTP 响应发送
- **异常**: 参数类型错误时抛出错误

