#ifdef ESP32_PLATFORM

#include <ESP32Driver/Esp32Driver.h>

#if ESP32_SPI_ENABLED

#include <driver/spi_master.h>

#include <unordered_map>
#include <vector>

#include "BuildinStdlib.h"
#include "GC.h"
#include "VM.h"

#pragma region spi_support

#define SPI_MASTER_TIMEOUT_MS 1000

// SPI传输模式 - 添加NEXUS_前缀避免命名冲突
enum NEXUS_SPIMode {
  NEXUS_SPI_MODE_0 = 0,  // CPOL=0, CPHA=0
  NEXUS_SPI_MODE_1,      // CPOL=0, CPHA=1
  NEXUS_SPI_MODE_2,      // CPOL=1, CPHA=0
  NEXUS_SPI_MODE_3       // CPOL=1, CPHA=1
};

// SPI位序 - 添加NEXUS_前缀避免命名冲突
enum NEXUS_SPIBitOrder { NEXUS_SPI_MSBFIRST = 0, NEXUS_SPI_LSBFIRST };

// 存储SPI实例
struct SPIInstance {
  spi_device_handle_t device_handle;
  spi_host_device_t host;
  int cs_pin;
  int mode;
  int clock_speed;
  int bit_order;
  int data_bits;
};

static std::unordered_map<uint32_t, SPIInstance> spiInstances;
static uint32_t spiInstanceIdSeed = 1;

// 将模式转换为ESP32 SPI模式 - 不再定义spi_mode_t，直接使用int
int get_nexus_spi_mode(int mode) {
  // 将我们的模式转换为ESP-IDF的spi_mode_t
  switch (mode) {
    case NEXUS_SPI_MODE_0:
      return 0;  // SPI_MODE0
    case NEXUS_SPI_MODE_1:
      return 1;  // SPI_MODE1
    case NEXUS_SPI_MODE_2:
      return 2;  // SPI_MODE2
    case NEXUS_SPI_MODE_3:
      return 3;  // SPI_MODE3
    default:
      return 0;
  }
}

// 获取SPI主机设备
spi_host_device_t get_spi_host(int bus_num) {
  switch (bus_num) {
    case 1:
      return SPI2_HOST;  // HSPI对应SPI2_HOST
    case 2:
      return SPI3_HOST;  // VSPI对应SPI3_HOST
    default:
      return SPI2_HOST;  // 默认为HSPI
  }
}

// 获取SPI引脚配置
void get_spi_pins(spi_host_device_t host, int* mosi, int* miso, int* sclk) {
  switch (host) {
    case SPI2_HOST:          // HSPI
      if (mosi) *mosi = 13;  // HSPI MOSI
      if (miso) *miso = 12;  // HSPI MISO
      if (sclk) *sclk = 14;  // HSPI SCLK
      break;
    case SPI3_HOST:          // VSPI
      if (mosi) *mosi = 23;  // VSPI MOSI
      if (miso) *miso = 19;  // VSPI MISO
      if (sclk) *sclk = 18;  // VSPI SCLK
      break;
    default:
      if (mosi) *mosi = 13;
      if (miso) *miso = 12;
      if (sclk) *sclk = 14;
  }
}
// SPI传输函数 - 修复版本
esp_err_t spi_master_transmit(spi_device_handle_t handle,
                              const uint8_t* tx_data, uint8_t* rx_data,
                              size_t tx_length, size_t rx_length) {
  if (rx_length == 0 && tx_length == 0) {
    return ESP_OK;
  }

  spi_transaction_t trans = {};

  if (tx_length > 0) {
    trans.length = tx_length * 8;  // 总位数
    trans.tx_buffer = tx_data;
  }

  if (rx_length > 0) {
    trans.rxlength = rx_length * 8;  // 接收位数
    trans.rx_buffer = rx_data;
  } else {
    trans.rxlength = 0;
    trans.rx_buffer = nullptr;
  }

  trans.flags = 0;

  return spi_device_transmit(handle, &trans);
}

// 释放SPI设备
esp_err_t spi_master_delete(spi_device_handle_t handle) {
  return spi_bus_remove_device(handle);
}

std::unordered_map<std::string, VariableValue> SPIObjectFuncTemplate;

void SPIObjectFuncTemplateInit() {
  // 析构函数
  SPIObjectFuncTemplate["finalize"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) != spiInstances.end()) {
          spi_master_delete(spiInstances[instanceId].device_handle);
          spi_bus_free(spiInstances[instanceId].host);
          spiInstances.erase(instanceId);
        }

        return CreateBooleanVariable(true);
      });

  // 传输数据（全双工）
  SPIObjectFuncTemplate["transfer"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        if (args[0].getContentType() != ValueType::OBJECT ||
            args[1].getContentType() != ValueType::OBJECT) {
        }

        ByteBufferInfo txBuffer;
        ByteBufferInfo rxBuffer;
        auto& obj = args[0].content.ref->implement.objectImpl;
        if (obj.find("bufid") != obj.end()) {
          // Buffer对象
          uint32_t bufid = obj["bufid"].content.number;
          txBuffer = GetByteBufferInfo(bufid);
          if (!txBuffer.data) {
            currentWorker->ThrowError("invalid buffer");
            return VariableValue();
          }
        }
        obj = args[1].content.ref->implement.objectImpl;
        if (obj.find("bufid") != obj.end()) {
          // Buffer对象
          uint32_t bufid = obj["bufid"].content.number;
          rxBuffer = GetByteBufferInfo(bufid);
          if (!rxBuffer.data) {
            currentWorker->ThrowError("invalid buffer");
            return VariableValue();
          }
        }

        // 执行SPI传输
        esp_err_t ret = spi_master_transmit(spi_inst.device_handle,
                                            txBuffer.data, rxBuffer.data,
                                            txBuffer.length, rxBuffer.length);

        if (ret != ESP_OK) {
          char error_msg[100];
          snprintf(error_msg, sizeof(error_msg), "SPI transfer failed: %d",
                   ret);
          return CreateBooleanVariable(false);
        }

        return CreateBooleanVariable(true);
      });

  // 只发送数据  write(payload : number | Buffer)
  SPIObjectFuncTemplate["write"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];
        std::vector<uint8_t> tx_data;

        // 处理发送数据
        if (args[0].getContentType() == ValueType::NUM) {
          // 单个字节
          tx_data.push_back((uint8_t)args[0].content.number);
        } else if (args[0].getContentType() == ValueType::OBJECT) {
          auto& obj = args[0].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            // Buffer对象
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data || bufinfo.length == 0) {
              currentWorker->ThrowError("Invalid Buffer");
              return VariableValue();
            }

            // 执行SPI发送
            esp_err_t ret =
                spi_master_transmit(spi_inst.device_handle, bufinfo.data,
                                    nullptr, bufinfo.length, 0);
            if (ret != ESP_OK) printf("SPI write failed: %d", ret);
            return CreateBooleanVariable(ret == ESP_OK);
          } else {
            currentWorker->ThrowError(
                "Data argument must be a number or Buffer");
            return VariableValue();
          }
        } else {
          currentWorker->ThrowError("Data argument must be a number or Buffer");
          return VariableValue();
        }

        if (tx_data.empty()) {
          currentWorker->ThrowError("No data to write");
          return VariableValue();
        }

        // 执行SPI发送
        esp_err_t ret = spi_master_transmit(
            spi_inst.device_handle, tx_data.data(), nullptr, tx_data.size(), 0);

        if (ret != ESP_OK) {
          printf("SPI write failed: %d", ret);

          return CreateBooleanVariable(false);
        }
        return CreateBooleanVariable(ret == ESP_OK);
      });

  // 只接收数据 read(buf : Buffer,length : number)
  SPIObjectFuncTemplate["read"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::OBJECT ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument");
          return VariableValue();
        }

        size_t length = (size_t)args[1].content.number;

        if (length <= 0 || length > 4096) {
          currentWorker->ThrowError("Read length must be between 1 and 4096");
          return VariableValue();
        }

        auto bufid_it = args[0].content.ref->implement.objectImpl.find("bufid");
        if (bufid_it == args[0].content.ref->implement.objectImpl.end()) {
          currentWorker->ThrowError("Invalid buffer object");
          return VariableValue();
        }
        uint32_t bufid = (uint32_t)(*bufid_it).second.content.number;

        auto buf_info = GetByteBufferInfo(bufid);

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

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        // 准备接收缓冲区
        // std::vector<uint8_t> rx_data(length, 0);
        std::vector<uint8_t> tx_data(length, 0xFF);  // 发送全1以读取数据

        // 执行SPI传输
        esp_err_t ret =
            spi_master_transmit(spi_inst.device_handle, tx_data.data(),
                                buf_info.data, length, length);

        if (ret != ESP_OK) {
          printf("SPI read failed: %d", ret);
          return CreateBooleanVariable(false);
        }

        return CreateBooleanVariable(true);
      });

  // 传输16位数据
  SPIObjectFuncTemplate["transfer16"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument: expected 16-bit data");
          return VariableValue();
        }

        uint16_t tx_data = (uint16_t)args[0].content.number;
        uint16_t rx_data = 0;

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        // 准备SPI事务
        spi_transaction_t trans = {};
        trans.length = 16;           // 16位数据
        trans.rxlength = 16;         // 接收16位
        trans.tx_buffer = &tx_data;  // 发送数据
        trans.rx_buffer = &rx_data;  // 接收数据

        // 执行传输
        esp_err_t ret = spi_device_transmit(spi_inst.device_handle, &trans);

        if (ret != ESP_OK) {
          printf("SPI failed: %d", ret);
          return CreateBooleanVariable(false);
        }

        return CreateNumberVariable(rx_data);
      });

  // 传输32位数据
  SPIObjectFuncTemplate["transfer32"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument: expected 32-bit data");
          return VariableValue();
        }

        uint32_t tx_data = (uint32_t)args[0].content.number;
        uint32_t rx_data = 0;

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        // 准备SPI事务
        spi_transaction_t trans = {};
        trans.length = 32;           // 32位数据
        trans.rxlength = 32;         // 接收32位
        trans.tx_buffer = &tx_data;  // 发送数据
        trans.rx_buffer = &rx_data;  // 接收数据

        // 执行传输
        esp_err_t ret = spi_device_transmit(spi_inst.device_handle, &trans);

        if (ret != ESP_OK) {
          printf("SPI failed: %d", ret);
          return CreateBooleanVariable(false);
        }

        return CreateNumberVariable((double)rx_data);
      });

  // 设置CS引脚状态
  SPIObjectFuncTemplate["setCS"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::BOOL) {
          currentWorker->ThrowError("Invalid argument: expected boolean");
          return VariableValue();
        }

        bool cs_state = args[0].content.boolean;

        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        // 设置CS引脚状态
        gpio_set_direction((gpio_num_t)spi_inst.cs_pin, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)spi_inst.cs_pin, cs_state ? 0 : 1);

        return CreateBooleanVariable(true);
      });

  // 获取SPI配置信息
  SPIObjectFuncTemplate["getConfig"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) == spiInstances.end()) {
          currentWorker->ThrowError(
              "SPI instance not initialized or already released");
          return VariableValue();
        }

        SPIInstance& spi_inst = spiInstances[instanceId];

        VMObject* configObj =
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);
        auto& configContainer = configObj->implement.objectImpl;

        configContainer["bus"] =
            CreateNumberVariable(spi_inst.host == SPI2_HOST ? 1 : 2);
        configContainer["cs_pin"] = CreateNumberVariable(spi_inst.cs_pin);
        configContainer["clock_speed"] =
            CreateNumberVariable(spi_inst.clock_speed);
        configContainer["mode"] = CreateNumberVariable(spi_inst.mode);
        configContainer["bit_order"] = CreateNumberVariable(spi_inst.bit_order);
        configContainer["data_bits"] = CreateNumberVariable(spi_inst.data_bits);

        return CreateReferenceVariable(configObj);
      });

  // 释放资源
  SPIObjectFuncTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t instanceId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (spiInstances.find(instanceId) != spiInstances.end()) {
          spi_master_delete(spiInstances[instanceId].device_handle);
          spi_bus_free(spiInstances[instanceId].host);
          spiInstances.erase(instanceId);
        }

        return CreateBooleanVariable(true);
      });
}

void ESP32_SPI_Init(VM* VMInstance) {
  VMObject* spiClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);

  // 添加常量定义 - 使用NEXUS_前缀
  spiClass->implement.objectImpl["MODE_0"] =
      CreateNumberVariable(NEXUS_SPI_MODE_0);
  spiClass->implement.objectImpl["MODE_1"] =
      CreateNumberVariable(NEXUS_SPI_MODE_1);
  spiClass->implement.objectImpl["MODE_2"] =
      CreateNumberVariable(NEXUS_SPI_MODE_2);
  spiClass->implement.objectImpl["MODE_3"] =
      CreateNumberVariable(NEXUS_SPI_MODE_3);
  spiClass->implement.objectImpl["MSBFIRST"] =
      CreateNumberVariable(NEXUS_SPI_MSBFIRST);
  spiClass->implement.objectImpl["LSBFIRST"] =
      CreateNumberVariable(NEXUS_SPI_LSBFIRST);
  spiClass->implement.objectImpl["HSPI"] = CreateNumberVariable(1);  // HSPI总线
  spiClass->implement.objectImpl["VSPI"] = CreateNumberVariable(2);  // VSPI总线


  SPIObjectFuncTemplateInit();

  // 初始化SPI主机
  spiClass->implement.objectImpl["init"] = VM::CreateSystemFunc(
      6,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||  // bus_num
            args[1].getContentType() != ValueType::NUM ||  // cs_pin
            args[2].getContentType() != ValueType::NUM ||  // clock_speed
            args[3].getContentType() != ValueType::NUM ||  // mode
            args[4].getContentType() != ValueType::NUM ||  // bit_order
            args[5].getContentType() != ValueType::NUM) {  // data_bits
          currentWorker->ThrowError(
              "Invalid argument type: expected (bus_num, cs_pin, clock_speed, "
              "mode, bit_order, data_bits)");
          return VariableValue();
        }

        int bus_num = (int)args[0].content.number;
        int cs_pin = (int)args[1].content.number;
        int clock_speed = (int)args[2].content.number;
        int mode = (int)args[3].content.number;
        int bit_order = (int)args[4].content.number;
        int data_bits = (int)args[5].content.number;

        // 验证参数范围
        if (bus_num != 1 && bus_num != 2) {
          currentWorker->ThrowError("Bus number must be 1 (HSPI) or 2 (VSPI)");
          return VariableValue();
        }

        if (clock_speed < 100000 || clock_speed > 40000000) {
          currentWorker->ThrowError(
              "Clock speed must be between 100KHz and 40MHz");
          return VariableValue();
        }

        if (mode < NEXUS_SPI_MODE_0 || mode > NEXUS_SPI_MODE_3) {
          currentWorker->ThrowError("Mode must be between 0 and 3");
          return VariableValue();
        }

        if (data_bits != 8 && data_bits != 16 && data_bits != 32) {
          currentWorker->ThrowError("Data bits must be 8, 16, or 32");
          return VariableValue();
        }

        // 创建SPI对象
        VMObject* spiObject =
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);

        spiObject->implement.objectImpl =
            SPIObjectFuncTemplate;  // 拷贝对象模板过去

        auto& objContainer = spiObject->implement.objectImpl;

        // 分配实例ID
        uint32_t instanceId = spiInstanceIdSeed++;
        objContainer["_id"] = CreateNumberVariable((double)instanceId);
        objContainer["bus"] = CreateNumberVariable(bus_num);
        objContainer["cs"] = CreateNumberVariable(cs_pin);
        objContainer["clock"] = CreateNumberVariable(clock_speed);
        objContainer["mode"] = CreateNumberVariable(mode);
        objContainer["bit_order"] = CreateNumberVariable(bit_order);
        objContainer["data_bits"] = CreateNumberVariable(data_bits);

        // 获取SPI主机和引脚
        spi_host_device_t host = get_spi_host(bus_num);
        int mosi_pin, miso_pin, sclk_pin;
        get_spi_pins(host, &mosi_pin, &miso_pin, &sclk_pin);

        // 初始化SPI总线配置
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = mosi_pin;
        bus_config.miso_io_num = miso_pin;
        bus_config.sclk_io_num = sclk_pin;
        bus_config.quadwp_io_num = -1;
        bus_config.quadhd_io_num = -1;
        bus_config.max_transfer_sz = 4096;

        // 初始化设备配置
        spi_device_interface_config_t dev_config = {};
        dev_config.mode = get_nexus_spi_mode(mode);
        dev_config.clock_speed_hz = clock_speed;
        dev_config.spics_io_num = cs_pin;
        dev_config.queue_size = 7;

        // 设置位序标志
        if (bit_order == NEXUS_SPI_LSBFIRST) {
          dev_config.flags =
              SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST;
        }

        // 初始化SPI总线
        esp_err_t ret = spi_bus_initialize(host, &bus_config, SPI_DMA_CH_AUTO);
        if (ret != ESP_OK) {
          char error_msg[100];
          snprintf(error_msg, sizeof(error_msg),
                   "Failed to initialize SPI bus: %d", ret);
          currentWorker->ThrowError(
              std::string(error_msg, error_msg + strlen(error_msg)));
          return VariableValue();
        }

        // 添加设备到总线
        spi_device_handle_t device_handle;
        ret = spi_bus_add_device(host, &dev_config, &device_handle);
        if (ret != ESP_OK) {
          spi_bus_free(host);
          char error_msg[100];
          snprintf(error_msg, sizeof(error_msg), "Failed to add SPI device: %d",
                   ret);
          currentWorker->ThrowError(
              std::string(error_msg, error_msg + strlen(error_msg)));
          return VariableValue();
        }

        // 存储SPI实例
        SPIInstance spi_inst = {.device_handle = device_handle,
                                .host = host,
                                .cs_pin = cs_pin,
                                .mode = mode,
                                .clock_speed = clock_speed,
                                .bit_order = bit_order,
                                .data_bits = data_bits};
        spiInstances[instanceId] = spi_inst;

        return CreateReferenceVariable(spiObject);
      });

  std::string spiClassName = "SPI";
  auto spiClassRef = CreateReferenceVariable(spiClass);
  VMInstance->storeGlobalSymbol(spiClassName, spiClassRef);
}

#pragma endregion

#endif

#endif