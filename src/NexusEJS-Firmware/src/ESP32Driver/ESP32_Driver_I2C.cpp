#ifdef ESP32_PLATFORM

#include <ESP32Driver/Esp32Driver.h>

#if ESP32_I2C_ENABLED

#include <driver/i2c.h>

#include <unordered_map>
#include <vector>

#include "BuildinStdlib.h"
#include "GC.h"
#include "VM.h"

#pragma region i2c_support

#define I2C_MASTER_TIMEOUT_MS 1000

// 存储I2C实例
static std::unordered_map<uint32_t, i2c_port_t> i2cInstances;
static uint32_t i2cInstanceIdSeed = 1;

// 合并初始化和读写函数，通过参数指定I2C端口
void i2c_master_init(i2c_port_t i2c_num, uint8_t sda_pin, uint8_t scl_pin,
                     uint32_t freq) {
  i2c_config_t conf = {.mode = I2C_MODE_MASTER,
                       .sda_io_num = sda_pin,
                       .scl_io_num = scl_pin,
                       .sda_pullup_en = GPIO_PULLUP_ENABLE,
                       .scl_pullup_en = GPIO_PULLUP_ENABLE,
                       .master = {.clk_speed = freq},
                       .clk_flags = 0};

  i2c_param_config(i2c_num, &conf);
  i2c_driver_install(i2c_num, I2C_MODE_MASTER, 0, 0, 0);
}

esp_err_t i2c_master_write(i2c_port_t i2c_num, uint8_t addr,
                           const uint8_t* data, size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
  i2c_master_write(cmd, data, len, true);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(
      i2c_num, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

esp_err_t i2c_master_read(i2c_port_t i2c_num, uint8_t addr, uint8_t* data,
                          size_t len) {
  i2c_cmd_handle_t cmd = i2c_cmd_link_create();
  i2c_master_start(cmd);
  i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
  if (len > 1) {
    i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
  }
  i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
  i2c_master_stop(cmd);
  esp_err_t ret = i2c_master_cmd_begin(
      i2c_num, cmd, I2C_MASTER_TIMEOUT_MS / portTICK_PERIOD_MS);
  i2c_cmd_link_delete(cmd);
  return ret;
}

esp_err_t i2c_master_delete(i2c_port_t i2c_num) {
  return i2c_driver_delete(i2c_num);
}

std::unordered_map<std::string, VariableValue> I2CObjectFuncTemplate;

void I2C_ObjectFuncTemplateInit() {
  // 析构函数
  I2CObjectFuncTemplate["finalize"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (i2cInstances.find(instanceId) != i2cInstances.end()) {
          i2c_master_delete(i2cInstances[instanceId]);
          i2cInstances.erase(instanceId);
        }

        return CreateBooleanVariable(true);
      });

  // 写入数据
  I2CObjectFuncTemplate["write"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Device address must be a number");
          return VariableValue();
        }

        uint8_t addr = (uint8_t)args[0].content.number;

        // 获取I2C实例信息
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        i2c_port_t i2c_port =
            (i2c_port_t)thisValue->implement.objectImpl["_port"].content.number;

        if (i2cInstances.find(instanceId) == i2cInstances.end()) {
          currentWorker->ThrowError(
              "I2C instance not initialized or already released");
          return VariableValue();
        }

        // 处理数据参数
        if (args[1].getContentType() == ValueType::NUM) {
          // 单个字节
          uint8_t data = (uint8_t)args[1].content.number;
          esp_err_t ret = i2c_master_write(i2c_port, addr, &data, 1);
          return CreateBooleanVariable(ret == ESP_OK);
        } else if (args[1].getContentType() == ValueType::OBJECT) {
          // Buffer对象
          auto& obj = args[1].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data || bufinfo.length == 0) {
              currentWorker->ThrowError("Invalid Buffer");
              return VariableValue();
            }

            esp_err_t ret =
                i2c_master_write(i2c_port, addr, bufinfo.data, bufinfo.length);
            return CreateBooleanVariable(ret == ESP_OK);
          }
        }

        currentWorker->ThrowError(
            "Data argument must be a number, Buffer, or array");
        return VariableValue();
      });

  // 读取数据
  I2CObjectFuncTemplate["read"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::OBJECT ||
            args[1].getContentType() != ValueType::NUM ||
            args[2].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "Invalid argument type: expected (buf ,addr, length)");
          return VariableValue();
        }

        auto bufid_it = args[0].content.ref->implement.objectImpl.find("bufid");
        if (bufid_it == args[0].content.ref->implement.objectImpl.end()) {
          currentWorker->ThrowError("Invalid buffer object");
          return VariableValue();
        }
        uint32_t bufid = (*bufid_it).second.content.number;

        auto buf_info = GetByteBufferInfo(bufid);

        uint8_t addr = (uint8_t)args[1].content.number;
        size_t length = (size_t)args[2].content.number;

        if (length <= 0 || length > 1024) {
          currentWorker->ThrowError("Read length must be between 1 and 1024");
          return VariableValue();
        }

        if (!buf_info.data) {
          currentWorker->ThrowError("Invalid buffer object");
          return VariableValue();
        }

        if (buf_info.length < length) {
          currentWorker->ThrowError("the buffer size too low");
          return VariableValue();
        }

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        i2c_port_t i2c_port =
            (i2c_port_t)thisValue->implement.objectImpl["_port"].content.number;

        if (i2cInstances.find(instanceId) == i2cInstances.end()) {
          currentWorker->ThrowError(
              "I2C instance not initialized or already released");
          return VariableValue();
        }

        // 读取数据
        // std::vector<uint8_t> buffer(length);
        esp_err_t ret = i2c_master_read(i2c_port, addr, buf_info.data, length);

        if (ret != ESP_OK) {
          return CreateBooleanVariable(false);
        }

        return CreateBooleanVariable(true);
      });

  // 写入寄存器
  I2CObjectFuncTemplate["writeReg"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "Invalid argument type: expected (addr, reg, data)");
          return VariableValue();
        }

        uint8_t addr = (uint8_t)args[0].content.number;
        uint8_t reg = (uint8_t)args[1].content.number;

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        i2c_port_t i2c_port =
            (i2c_port_t)thisValue->implement.objectImpl["_port"].content.number;

        if (i2cInstances.find(instanceId) == i2cInstances.end()) {
          currentWorker->ThrowError(
              "I2C instance not initialized or already released");
          return VariableValue();
        }

        // 处理数据
        std::vector<uint8_t> data;
        data.push_back(reg);

        if (args[2].getContentType() == ValueType::NUM) {
          data.push_back((uint8_t)args[2].content.number);
        } else if (args[2].getContentType() == ValueType::OBJECT) {
          auto& obj = args[2].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data || bufinfo.length == 0) {
              currentWorker->ThrowError("Invalid Buffer");
              return VariableValue();
            }
            data.insert(data.end(), bufinfo.data,
                        bufinfo.data + bufinfo.length);
          } else {
            currentWorker->ThrowError("invaild buffer object");
            return VariableValue();
          }
        } else {
          currentWorker->ThrowError("invaild argument");
          return VariableValue();
        }

        esp_err_t ret =
            i2c_master_write(i2c_port, addr, data.data(), data.size());
        return CreateBooleanVariable(ret == ESP_OK);
      });

  // 读取寄存器
  I2CObjectFuncTemplate["readReg"] = VM::CreateSystemFunc(
      4,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM ||
            args[2].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "Invalid argument type: expected (addr, reg, length)");
          return VariableValue();
        }

        uint8_t addr = (uint8_t)args[0].content.number;
        uint8_t reg = (uint8_t)args[1].content.number;
        size_t length = (size_t)args[2].content.number;

        if (length <= 0 || length > 1024) {
          currentWorker->ThrowError("Read length must be between 1 and 1024");
          return VariableValue();
        }

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        i2c_port_t i2c_port =
            (i2c_port_t)thisValue->implement.objectImpl["_port"].content.number;

        if (i2cInstances.find(instanceId) == i2cInstances.end()) {
          currentWorker->ThrowError(
              "I2C instance not initialized or already released");
          return VariableValue();
        }

        // 先写寄存器地址
        esp_err_t ret = i2c_master_write(i2c_port, addr, &reg, 1);
        if (ret != ESP_OK) {
          return CreateBooleanVariable(false);
        }

        // 读取数据
        // std::vector<uint8_t> buffer(length);

        auto bufid_it = args[0].content.ref->implement.objectImpl.find("bufid");
        if (bufid_it == args[0].content.ref->implement.objectImpl.end()) {
          currentWorker->ThrowError("Invalid buffer object");
          return VariableValue();
        }
        uint32_t bufid = (*bufid_it).second.content.number;

        auto buf_info = GetByteBufferInfo(bufid);

        if (buf_info.length < length) {
          currentWorker->ThrowError("Invalid length");
          return VariableValue();
        }

        ret = i2c_master_read(i2c_port, addr, buf_info.data, length);

        if (ret != ESP_OK) {
          return CreateBooleanVariable(false);
        }

        return CreateBooleanVariable(true);
      });

  // 扫描设备
  I2CObjectFuncTemplate["scan"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        i2c_port_t i2c_port =
            (i2c_port_t)thisValue->implement.objectImpl["_port"].content.number;

        if (i2cInstances.find(instanceId) == i2cInstances.end()) {
          currentWorker->ThrowError(
              "I2C instance not initialized or already released");
          return VariableValue();
        }

        VMObject* arrayObj = currentWorker->VMInstance->currentGC->GC_NewObject(
            ValueType::ARRAY);

        // 扫描I2C地址 (0x08 到 0x77)
        for (int addr = 0x08; addr <= 0x77; addr++) {
          i2c_cmd_handle_t cmd = i2c_cmd_link_create();
          i2c_master_start(cmd);
          i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
          i2c_master_stop(cmd);

          esp_err_t ret =
              i2c_master_cmd_begin(i2c_port, cmd, 50 / portTICK_PERIOD_MS);
          i2c_cmd_link_delete(cmd);

          if (ret == ESP_OK) {
            arrayObj->implement.arrayImpl.push_back(CreateNumberVariable(addr));
          }

          vTaskDelay(1 / portTICK_PERIOD_MS);
        }

        return CreateReferenceVariable(arrayObj);
      });

  // 释放资源
  I2CObjectFuncTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (i2cInstances.find(instanceId) != i2cInstances.end()) {
          i2c_master_delete(i2cInstances[instanceId]);
          i2cInstances.erase(instanceId);
        }

        return CreateBooleanVariable(true);
      });
}

void ESP32_I2C_Init(VM* VMInstance) {
  VMObject* i2cClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);

  I2C_ObjectFuncTemplateInit();

  // 初始化I2C
  i2cClass->implement.objectImpl["init"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM ||
            args[2].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError(
              "Invalid argument type: expected (sda_pin, scl_pin, freq)");
          return VariableValue();
        }

        int sda_pin = (int)args[0].content.number;
        int scl_pin = (int)args[1].content.number;
        int freq = (int)args[2].content.number;

        // 创建I2C对象
        VMObject* i2cObject =
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);

        i2cObject->implement.objectImpl =
            I2CObjectFuncTemplate;  // 拷贝I2C内置成员到对象

        auto& objContainer = i2cObject->implement.objectImpl;

        // 分配实例ID
        uint32_t instanceId = i2cInstanceIdSeed++;
        objContainer["_id"] = CreateNumberVariable((double)instanceId);
        objContainer["sda"] = CreateNumberVariable(sda_pin);
        objContainer["sc"] = CreateNumberVariable(scl_pin);
        objContainer["freq"] = CreateNumberVariable(freq);

        // 确定使用哪个I2C端口
        i2c_port_t i2c_port = (instanceId % 2 == 0) ? I2C_NUM_0 : I2C_NUM_1;
        objContainer["_port"] = CreateNumberVariable((double)i2c_port);

        // 存储实例
        i2cInstances[instanceId] = i2c_port;

        // 初始化I2C
        i2c_master_init(i2c_port, sda_pin, scl_pin, freq);

        return CreateReferenceVariable(i2cObject);
      });

  std::string i2cClassName = "I2C";
  auto i2cClassRef = CreateReferenceVariable(i2cClass);
  VMInstance->storeGlobalSymbol(i2cClassName, i2cClassRef);
}

#pragma endregion

#endif

#endif