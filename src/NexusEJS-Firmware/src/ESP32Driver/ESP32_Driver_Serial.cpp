#ifdef ESP32_PLATFORM

#include <ESP32Driver/Esp32Driver.h>

#if ESP32_SERIALAPI_ENABLED


//暂定使用esp-idf的原生串口api，可以和println&c的printf使用的隔离开
#include "driver/uart.h"
#include "freertos/queue.h"
#include <unordered_map>
#include <vector>

#include "BuildinStdlib.h"
#include "GC.h"
#include "VM.h"

#pragma region serial_support

#define SERIAL_BUFFER_SIZE 1024
#define SERIAL_DEFAULT_QUEUE_SIZE 128
#define SERIAL_READ_TIMEOUT_MS 100
#define SERIAL_WRITE_TIMEOUT_MS 1000

#ifndef UART_SCLK_DEFAULT
#define UART_SCLK_DEFAULT UART_SCLK_APB
#endif

// 存储Serial实例
static std::unordered_map<uint32_t, uart_port_t> serialInstances;
static std::unordered_map<uint32_t, QueueHandle_t> serialQueues;
static uint32_t serialInstanceIdSeed = 1;

// 串口配置结构
/*
struct SerialConfig {
    int baud_rate;
    uart_word_length_t data_bits;
    uart_parity_t parity;
    uart_stop_bits_t stop_bits;
    uart_hw_flowcontrol_t flow_control;
};
*/

// 合并串口初始化函数
bool serial_port_init(uart_port_t uart_num, int tx_pin, int rx_pin, 
                      int baud_rate, uart_word_length_t data_bits,
                      uart_parity_t parity, uart_stop_bits_t stop_bits,
                      uart_hw_flowcontrol_t flow_control) {
    
    uart_config_t uart_config = {
        .baud_rate = baud_rate,
        .data_bits = data_bits,
        .parity = parity,
        .stop_bits = stop_bits,
        .flow_ctrl = flow_control,
        .rx_flow_ctrl_thresh = 122,
        .source_clk = UART_SCLK_DEFAULT,
    };
    
    // 先检查UART是否已安装
    bool driver_installed = uart_is_driver_installed(uart_num);
    
    if (!driver_installed) {
        // 安装驱动
        if(uart_driver_install(uart_num, SERIAL_BUFFER_SIZE * 2, 
                                        SERIAL_BUFFER_SIZE * 2, SERIAL_DEFAULT_QUEUE_SIZE, 
                                        nullptr, 0) != ESP_OK){
            return false;
        }
    } else {
        // 如果已安装，先卸载再重新安装
        uart_driver_delete(uart_num);
        if(uart_driver_install(uart_num, SERIAL_BUFFER_SIZE * 2, 
                                        SERIAL_BUFFER_SIZE * 2, SERIAL_DEFAULT_QUEUE_SIZE, 
                                        nullptr, 0) != ESP_OK){
            return false;
        }
    }
    
    if(uart_param_config(uart_num, &uart_config) != ESP_OK){
        return false;
    }
    
    // 设置引脚
    if(uart_set_pin(uart_num, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK){
        return false;
    }
    
    return true;
}

// 写入数据
esp_err_t serial_write(uart_port_t uart_num, const uint8_t* data, size_t len) {
    int bytes_written = uart_write_bytes(uart_num, (const char*)data, len);
    if (bytes_written < 0) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 读取数据
int serial_read(uart_port_t uart_num, uint8_t* data, size_t len, int timeout_ms) {
    return uart_read_bytes(uart_num, data, len, pdMS_TO_TICKS(timeout_ms));
}

// 获取可读字节数
int serial_available(uart_port_t uart_num) {
    size_t available = 0;
    if(uart_get_buffered_data_len(uart_num, &available) != ESP_OK);
    return available;
}

// 清理串口
esp_err_t serial_delete(uart_port_t uart_num) {
    return uart_driver_delete(uart_num);
}

// 刷新输出缓冲区
esp_err_t serial_flush(uart_port_t uart_num) {
    return uart_wait_tx_done(uart_num, pdMS_TO_TICKS(SERIAL_WRITE_TIMEOUT_MS));
}

std::unordered_map<std::string, VariableValue> SerialObjectFuncTemplate;

void Serial_ObjectFuncTemplateInit() {
    // 析构函数
    SerialObjectFuncTemplate["finalize"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

            printf("Serial.finalize id=%d\n",instanceId);

            if (serialInstances.find(instanceId) != serialInstances.end()) {
                serial_delete(serialInstances[instanceId]);
                serialInstances.erase(instanceId);
            }
            
            if (serialQueues.find(instanceId) != serialQueues.end()) {
                vQueueDelete(serialQueues[instanceId]);
                serialQueues.erase(instanceId);
            }

            return CreateBooleanVariable(true);
        });

    // 写入数据
    SerialObjectFuncTemplate["write"] = VM::CreateSystemFunc(
        1,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            // 处理数据参数
            esp_err_t ret = ESP_OK;
            
            if (args[0].getContentType() == ValueType::NUM) {
                // 单个字节
                uint8_t data = (uint8_t)args[0].content.number;
                ret = serial_write(uart_num, &data, 1);
            } else if (args[0].getContentType() == ValueType::OBJECT) {
                // Buffer对象
                auto& obj = args[0].content.ref->implement.objectImpl;
                if (obj.find("bufid") != obj.end()) {
                    uint32_t bufid = obj["bufid"].content.number;
                    auto bufinfo = GetByteBufferInfo(bufid);
                    if (!bufinfo.data || bufinfo.length == 0) {
                        currentWorker->ThrowError("Invalid Buffer");
                        return VariableValue();
                    }

                    ret = serial_write(uart_num, bufinfo.data, bufinfo.length);
                } else if (obj.find("string") != obj.end()) {
                    // 字符串对象
                    std::string str = obj["string"].content.ref->implement.stringImpl;
                    ret = serial_write(uart_num, (const uint8_t*)str.c_str(), str.length());
                } else {
                    currentWorker->ThrowError("Invalid data object");
                    return VariableValue();
                }
            } else if (args[0].getContentType() == ValueType::STRING) {
                // 字符串值
                std::string str = args[0].content.ref->implement.stringImpl;
                ret = serial_write(uart_num, (const uint8_t*)str.c_str(), str.length());
            } else {
                currentWorker->ThrowError("Data argument must be a number, string, or Buffer");
                return VariableValue();
            }

            return CreateBooleanVariable(ret == ESP_OK);
        });

    // 读取数据
    SerialObjectFuncTemplate["read"] = VM::CreateSystemFunc(
        2,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            if (args[0].getContentType() != ValueType::OBJECT ||
                args[1].getContentType() != ValueType::NUM) {
                currentWorker->ThrowError(
                    "Invalid argument type: expected (buffer, length)");
                return VariableValue();
            }

            auto bufid_it = args[0].content.ref->implement.objectImpl.find("bufid");
            if (bufid_it == args[0].content.ref->implement.objectImpl.end()) {
                currentWorker->ThrowError("Invalid buffer object");
                return VariableValue();
            }
            uint32_t bufid = (*bufid_it).second.content.number;

            auto buf_info = GetByteBufferInfo(bufid);

            size_t length = (size_t)args[1].content.number;
            int timeout = SERIAL_READ_TIMEOUT_MS;

            if (args.size() > 2 && args[2].getContentType() == ValueType::NUM) {
                timeout = (int)args[2].content.number;
            }

            if (length <= 0 || length > 4096) {
                currentWorker->ThrowError("Read length must be between 1 and 4096");
                return VariableValue();
            }

            if (!buf_info.data) {
                currentWorker->ThrowError("Invalid buffer object");
                return VariableValue();
            }

            if (buf_info.length < length) {
                currentWorker->ThrowError("Buffer size too small");
                return VariableValue();
            }

            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            // 读取数据
            int bytes_read = serial_read(uart_num, buf_info.data, length, timeout);
            
            if (bytes_read < 0) {
                return CreateBooleanVariable(false);
            }

            // 返回实际读取的字节数
            return CreateNumberVariable((double)bytes_read);
        });

    // 读取字节
    SerialObjectFuncTemplate["readByte"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            uint8_t byte = 0;
            int bytes_read = serial_read(uart_num, &byte, 1, SERIAL_READ_TIMEOUT_MS);
            
            if (bytes_read <= 0) {
                return CreateNumberVariable(-1); // 返回-1表示无数据
            }

            return CreateNumberVariable((double)byte);
        });

    // 读取一行
    SerialObjectFuncTemplate["readLine"] = VM::CreateSystemFunc(
        DYNAMIC_ARGUMENT,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            // 创建字符串缓冲区
            VMObject* strObj = currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::STRING);
            std::string line = "";
            
            uint8_t byte = 0;
            int timeout = 100; // 每字节超时
            int line_timeout = 1000; // 行读取总超时
            if(args.size() > 0 && args[0].varType == ValueType::NUM){
                line_timeout = (int)args[0].content.number;
            }
            int start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            
            while (true) {
                int bytes_read = serial_read(uart_num, &byte, 1, timeout);
                
                if (bytes_read > 0) {
                    if (byte == '\n') {
                        break;
                    } else if (byte != '\r') {
                        line += (char)byte;
                    }
                }
                
                // 检查超时
                if ((xTaskGetTickCount() * portTICK_PERIOD_MS - start_time) > line_timeout) {
                    break;
                }
                
                taskYIELD();
            }
            
            strObj->implement.stringImpl = line;
            return CreateReferenceVariable(strObj);
        });

    // 获取可读字节数
    SerialObjectFuncTemplate["available"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            int available = serial_available(uart_num);
            return CreateNumberVariable((double)available);
        });

    // 刷新输出缓冲区
    SerialObjectFuncTemplate["flush"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            esp_err_t ret = serial_flush(uart_num);
            return CreateBooleanVariable(ret == ESP_OK);
        });

    // 清空输入缓冲区
    SerialObjectFuncTemplate["clear"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            ESP_ERROR_CHECK(uart_flush(uart_num));
            return CreateBooleanVariable(true);
        });

    // 设置串口参数
    SerialObjectFuncTemplate["setConfig"] = VM::CreateSystemFunc(
        4,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            if (args[0].getContentType() != ValueType::NUM ||
                args[1].getContentType() != ValueType::NUM ||
                args[2].getContentType() != ValueType::NUM ||
                args[3].getContentType() != ValueType::NUM) {
                currentWorker->ThrowError(
                    "Invalid argument type: expected (baudRate, dataBits, parity, stopBits)");
                return VariableValue();
            }

            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            uart_port_t uart_num =
                (uart_port_t)thisValue->implement.objectImpl["_port"].content.number;

            if (serialInstances.find(instanceId) == serialInstances.end()) {
                currentWorker->ThrowError(
                    "Serial instance not initialized or already released");
                return VariableValue();
            }

            int baud_rate = (int)args[0].content.number;
            int data_bits = (int)args[1].content.number;
            int parity = (int)args[2].content.number;
            int stop_bits = (int)args[3].content.number;

            // 验证参数
            if (data_bits < 5 || data_bits > 8) {
                currentWorker->ThrowError("Data bits must be 5, 6, 7, or 8");
                return VariableValue();
            }

            uart_word_length_t word_length = UART_DATA_5_BITS;
            if (data_bits == 5) word_length = UART_DATA_5_BITS;
            else if (data_bits == 6) word_length = UART_DATA_6_BITS;
            else if (data_bits == 7) word_length = UART_DATA_7_BITS;
            else if (data_bits == 8) word_length = UART_DATA_8_BITS;

            uart_parity_t parity_mode = UART_PARITY_DISABLE;
            if (parity == 0) parity_mode = UART_PARITY_DISABLE;
            else if (parity == 1) parity_mode = UART_PARITY_EVEN;
            else if (parity == 2) parity_mode = UART_PARITY_ODD;

            uart_stop_bits_t stop_bits_mode = UART_STOP_BITS_1;
            if (stop_bits == 1) stop_bits_mode = UART_STOP_BITS_1;
            else if (stop_bits == 2) stop_bits_mode = UART_STOP_BITS_2;
            else if (stop_bits == 15) stop_bits_mode = UART_STOP_BITS_1_5;

            uart_config_t uart_config = {
                .baud_rate = baud_rate,
                .data_bits = word_length,
                .parity = parity_mode,
                .stop_bits = stop_bits_mode,
                .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
                .rx_flow_ctrl_thresh = 122,
                .source_clk = UART_SCLK_DEFAULT,
            };

            esp_err_t ret = uart_param_config(uart_num, &uart_config);
            
            // 更新对象属性
            thisValue->implement.objectImpl["baudRate"] = CreateNumberVariable(baud_rate);
            thisValue->implement.objectImpl["dataBits"] = CreateNumberVariable(data_bits);
            thisValue->implement.objectImpl["parity"] = CreateNumberVariable(parity);
            thisValue->implement.objectImpl["stopBits"] = CreateNumberVariable(stop_bits);

            return CreateBooleanVariable(ret == ESP_OK);
        });

    // 获取当前配置
    SerialObjectFuncTemplate["getConfig"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            VMObject* configObj = currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);
            
            // 从对象属性获取配置
            auto& obj = thisValue->implement.objectImpl;
            configObj->implement.objectImpl["baudRate"] = obj["baudRate"];
            configObj->implement.objectImpl["dataBits"] = obj["dataBits"];
            configObj->implement.objectImpl["parity"] = obj["parity"];
            configObj->implement.objectImpl["stopBits"] = obj["stopBits"];
            configObj->implement.objectImpl["flowControl"] = obj["flowControl"];
            configObj->implement.objectImpl["txPin"] = obj["txPin"];
            configObj->implement.objectImpl["rxPin"] = obj["rxPin"];

            return CreateReferenceVariable(configObj);
        });

    // 关闭串口
    SerialObjectFuncTemplate["close"] = VM::CreateSystemFunc(
        0,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            uint32_t instanceId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

            if (serialInstances.find(instanceId) != serialInstances.end()) {
                serial_delete(serialInstances[instanceId]);
                serialInstances.erase(instanceId);
            }
            
            if (serialQueues.find(instanceId) != serialQueues.end()) {
                vQueueDelete(serialQueues[instanceId]);
                serialQueues.erase(instanceId);
            }

            return CreateBooleanVariable(true);
        });
}

void ESP32_Serial_Init(VM* VMInstance) {
    VMObject* serialClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);

    Serial_ObjectFuncTemplateInit();

    // 初始化串口（简单版本）
    serialClass->implement.objectImpl["init"] = VM::CreateSystemFunc(
        DYNAMIC_ARGUMENT,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            if (args[0].getContentType() != ValueType::NUM ||
                args[1].getContentType() != ValueType::NUM ||
                args[2].getContentType() != ValueType::NUM) {
                currentWorker->ThrowError(
                    "Invalid argument type: expected (txPin, rxPin, baudRate)");
                return VariableValue();
            }

            int tx_pin = (int)args[0].content.number;
            int rx_pin = (int)args[1].content.number;
            int baud_rate = (int)args[2].content.number;

            // 默认参数
            int data_bits = 8;
            int parity = 0; // 0: none, 1: even, 2: odd
            int stop_bits = 1;
            int flow_control = 0; // 0: none, 1: RTS, 2: CTS, 3: RTS|CTS

            // 可选参数
            if (args.size() > 3 && args[3].getContentType() == ValueType::NUM) {
                data_bits = (int)args[3].content.number;
            }
            if (args.size() > 4 && args[4].getContentType() == ValueType::NUM) {
                parity = (int)args[4].content.number;
            }
            if (args.size() > 5 && args[5].getContentType() == ValueType::NUM) {
                stop_bits = (int)args[5].content.number;
            }
            if (args.size() > 6 && args[6].getContentType() == ValueType::NUM) {
                flow_control = (int)args[6].content.number;
            }


            // 确定使用哪个UART端口（ESP32有3个UART）
            uart_port_t uart_port = UART_NUM_0;
            if (rx_pin == 16 || tx_pin == 17) {
                uart_port = UART_NUM_2;  // UART2通常使用GPIO16(RX), GPIO17(TX)
            } else if (rx_pin == 32 || tx_pin == 33) {
                uart_port = UART_NUM_1;  // UART1通常使用GPIO32(RX), GPIO33(TX)
            } else {
                uart_port = UART_NUM_0;  // 默认使用UART0
            }
            
            // 转换参数
            uart_word_length_t word_length = UART_DATA_8_BITS;
            if (data_bits == 5) word_length = UART_DATA_5_BITS;
            else if (data_bits == 6) word_length = UART_DATA_6_BITS;
            else if (data_bits == 7) word_length = UART_DATA_7_BITS;
            
            uart_parity_t parity_mode = UART_PARITY_DISABLE;
            if (parity == 1) parity_mode = UART_PARITY_EVEN;
            else if (parity == 2) parity_mode = UART_PARITY_ODD;
            
            uart_stop_bits_t stop_bits_mode = UART_STOP_BITS_1;
            if (stop_bits == 2) stop_bits_mode = UART_STOP_BITS_2;
            else if (stop_bits == 15) stop_bits_mode = UART_STOP_BITS_1_5;
            
            uart_hw_flowcontrol_t flow_control_mode = UART_HW_FLOWCTRL_DISABLE;
            if (flow_control == 1) flow_control_mode = UART_HW_FLOWCTRL_RTS;
            else if (flow_control == 2) flow_control_mode = UART_HW_FLOWCTRL_CTS;
            else if (flow_control == 3) flow_control_mode = UART_HW_FLOWCTRL_CTS_RTS;

            // 初始化串口
            bool success = serial_port_init(uart_port, tx_pin, rx_pin, baud_rate, 
                           word_length, parity_mode, stop_bits_mode, 
                           flow_control_mode);

            if(!success){
                currentWorker->ThrowError("Serial.init failed");
                return VariableValue();
            }

            // 创建Serial对象
            VMObject* serialObject =
                currentWorker->VMInstance->currentGC->GC_NewObject(
                    ValueType::OBJECT);

            serialObject->implement.objectImpl =
                SerialObjectFuncTemplate;  // 拷贝Serial内置成员到对象

            auto& objContainer = serialObject->implement.objectImpl;

            // 分配实例ID
            uint32_t instanceId = serialInstanceIdSeed++;
            objContainer["_id"] = CreateNumberVariable((double)instanceId);
            objContainer["txPin"] = CreateNumberVariable(tx_pin);
            objContainer["rxPin"] = CreateNumberVariable(rx_pin);
            objContainer["baudRate"] = CreateNumberVariable(baud_rate);
            objContainer["dataBits"] = CreateNumberVariable(data_bits);
            objContainer["parity"] = CreateNumberVariable(parity);
            objContainer["stopBits"] = CreateNumberVariable(stop_bits);
            objContainer["flowControl"] = CreateNumberVariable(flow_control);

            

            objContainer["_port"] = CreateNumberVariable((double)uart_port);

            
            
            

            // 存储实例
            serialInstances[instanceId] = uart_port;

            return CreateReferenceVariable(serialObject);
        });
        /*
    // 快速初始化（使用默认引脚）
    serialClass->implement.objectImpl["initDefault"] = VM::CreateSystemFunc(
        1,
        [](std::vector<VariableValue>& args, VMObject* thisValue,
           VMWorker* currentWorker) -> VariableValue {
            
            if (args[0].getContentType() != ValueType::NUM) {
                currentWorker->ThrowError(
                    "Invalid argument type: expected (baudRate)");
                return VariableValue();
            }

            int baud_rate = (int)args[0].content.number;
            
            // 默认使用UART0，GPIO1(TX), GPIO3(RX) - ESP32的默认串口引脚
            int tx_pin = 1;
            int rx_pin = 3;
            int data_bits = 8;
            int parity = 0;
            int stop_bits = 1;
            int flow_control = 0;

            VMObject* serialObject =
                currentWorker->VMInstance->currentGC->GC_NewObject(
                    ValueType::OBJECT);

            serialObject->implement.objectImpl = SerialObjectFuncTemplate;
            auto& objContainer = serialObject->implement.objectImpl;

            uint32_t instanceId = serialInstanceIdSeed++;
            objContainer["_id"] = CreateNumberVariable((double)instanceId);
            objContainer["txPin"] = CreateNumberVariable(tx_pin);
            objContainer["rxPin"] = CreateNumberVariable(rx_pin);
            objContainer["baudRate"] = CreateNumberVariable(baud_rate);
            objContainer["dataBits"] = CreateNumberVariable(data_bits);
            objContainer["parity"] = CreateNumberVariable(parity);
            objContainer["stopBits"] = CreateNumberVariable(stop_bits);
            objContainer["flowControl"] = CreateNumberVariable(flow_control);
            objContainer["_port"] = CreateNumberVariable((double)UART_NUM_0);

            // 初始化串口
            serial_port_init(UART_NUM_0, tx_pin, rx_pin, baud_rate, 
                           UART_DATA_8_BITS, UART_PARITY_DISABLE, 
                           UART_STOP_BITS_1, UART_HW_FLOWCTRL_DISABLE);

            serialInstances[instanceId] = UART_NUM_0;

            return CreateReferenceVariable(serialObject);
        });
        */

    std::string serialClassName = "Serial";
    auto serialClassRef = CreateReferenceVariable(serialClass);
    VMInstance->storeGlobalSymbol(serialClassName, serialClassRef);
}

#pragma endregion

#endif

#endif