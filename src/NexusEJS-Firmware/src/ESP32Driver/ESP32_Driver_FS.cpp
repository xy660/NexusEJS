#ifdef ESP32_PLATFORM

#include <ESP32Driver/Esp32Driver.h>

#include <SPIFFS.h>

#include "BuildinStdlib.h"
#include "GC.h"
#include "VM.h"

#pragma region spiffs_support

#include <unordered_map>
#include <vector>
#include <string>
#include <cstring>

// 存储文件句柄
static std::unordered_map<uint32_t, File> fileHandles;
static uint32_t fileHandleIdSeed = 1;

// 文件操作枚举
enum FileMode {
  NEXUS_FILE_READ = 0,
  NEXUS_FILE_WRITE = 1,
  NEXUS_FILE_APPEND = 2
};

std::unordered_map<std::string, VariableValue> FileObjectFuncTemplate;

void File_ObjectFuncTemplateInit() {
  // 关闭文件
  FileObjectFuncTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        
        auto it = fileHandles.find(fileId);
        if (it != fileHandles.end()) {
          it->second.close();
          fileHandles.erase(it);
          thisValue->implement.objectImpl.erase("finalize");
          return CreateBooleanVariable(true);
        }
        
        return CreateBooleanVariable(false);
      });
  
  // 读取文件
  FileObjectFuncTemplate["read"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::OBJECT ||
            args[1].getContentType() != ValueType::NUM ||
            args[2].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument type: expected (buffer, offset, length)");
          return VariableValue();
        }
        
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        auto& file = it->second;
        
        // 获取缓冲区
        auto& obj = args[0].content.ref->implement.objectImpl;
        if (obj.find("bufid") == obj.end()) {
          currentWorker->ThrowError("Invalid buffer object");
          return VariableValue();
        }
        
        uint32_t bufid = (uint32_t)obj["bufid"].content.number;
        auto buf_info = GetByteBufferInfo(bufid);
        if (!buf_info.data) {
          currentWorker->ThrowError("Invalid buffer");
          return VariableValue();
        }
        
        size_t offset = (size_t)args[1].content.number;
        size_t length = (size_t)args[2].content.number;
        
        if (offset + length > buf_info.length) {
          currentWorker->ThrowError("Buffer overflow");
          return VariableValue();
        }
        
        // 移动文件指针
        file.seek(offset, SeekSet);
        
        // 读取数据
        size_t bytesRead = file.read(buf_info.data, length);
        
        return CreateNumberVariable((double)bytesRead);
      });
  
  // 写入文件
  FileObjectFuncTemplate["write"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM ||
            (args[2].getContentType() != ValueType::OBJECT && args[2].getContentType() != ValueType::NUM)) {
          currentWorker->ThrowError("Invalid argument type: expected (offset, length, data)");
          return VariableValue();
        }
        
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        auto& file = it->second;
        size_t offset = (size_t)args[0].content.number;
        size_t length = (size_t)args[1].content.number;
        
        // 移动文件指针
        file.seek(offset, SeekSet);
        
        size_t bytesWritten = 0;
        
        if (args[2].getContentType() == ValueType::NUM) {
          // 写入单个字节
          uint8_t data = (uint8_t)args[2].content.number;
          bytesWritten = file.write(&data, 1);
        } else if (args[2].getContentType() == ValueType::OBJECT) {
          // 写入缓冲区
          auto& obj = args[2].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto buf_info = GetByteBufferInfo(bufid);
            if (!buf_info.data) {
              currentWorker->ThrowError("Invalid buffer");
              return VariableValue();
            }
            
            bytesWritten = file.write(buf_info.data, length);
          } else {
            currentWorker->ThrowError("Invalid buffer object");
            return VariableValue();
          }
        }
        
        return CreateNumberVariable((double)bytesWritten);
      });
  
  // 获取文件大小
  FileObjectFuncTemplate["size"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        return CreateNumberVariable((double)it->second.size());
      });
  
  // 获取当前位置
  FileObjectFuncTemplate["position"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        return CreateNumberVariable((double)it->second.position());
      });
  
  // 移动文件指针
  FileObjectFuncTemplate["seek"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument type: expected (offset, whence)");
          return VariableValue();
        }
        
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        long offset = (long)args[0].content.number;
        int whence = (int)args[1].content.number;
        
        SeekMode mode;
        switch (whence) {
          case 0: mode = SeekSet; break;  // 文件开头
          case 1: mode = SeekCur; break;  // 当前位置
          case 2: mode = SeekEnd; break;  // 文件末尾
          default:
            currentWorker->ThrowError("Invalid seek mode");
            return VariableValue();
        }
        
        bool result = it->second.seek(offset, mode);
        return CreateBooleanVariable(result);
      });
  
  // 刷新文件
  FileObjectFuncTemplate["flush"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto it = fileHandles.find(fileId);
        if (it == fileHandles.end()) {
          currentWorker->ThrowError("File not open or already closed");
          return VariableValue();
        }
        
        it->second.flush();
        return CreateBooleanVariable(true);
      });
  
  // 析构函数
  FileObjectFuncTemplate["finalize"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t fileId = (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        
        auto it = fileHandles.find(fileId);
        if (it != fileHandles.end()) {
          it->second.close();
          fileHandles.erase(it);
        }
        
        return CreateBooleanVariable(true);
      });
}

void ESP32_FSAPI_Init(VM* VMInstance) {
  // 初始化文件对象模板
  File_ObjectFuncTemplateInit();
  
  // 创建文件系统类
  VMObject* fsClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
  
  // 格式化文件系统
  fsClass->implement.objectImpl["format"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (!SPIFFS.begin(true)) {
          return CreateBooleanVariable(false);
        }
        
        bool result = SPIFFS.format();
        SPIFFS.end();
        return CreateBooleanVariable(result);
      });
  
  // 挂载文件系统
  fsClass->implement.objectImpl["mount"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (!SPIFFS.begin(true)) {
          return CreateBooleanVariable(false);
        }
        return CreateBooleanVariable(true);
      });
  
  // 卸载文件系统
  fsClass->implement.objectImpl["unmount"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        SPIFFS.end();
        return CreateBooleanVariable(true);
      });
  
  // 打开文件
  fsClass->implement.objectImpl["open"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("Invalid argument type: expected (path, mode)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        int mode = (int)args[1].content.number;
        
        const char* modeStr = "";
        switch (mode) {
          case NEXUS_FILE_READ: modeStr = "r"; break;
          case NEXUS_FILE_WRITE: modeStr = "w"; break;
          case NEXUS_FILE_APPEND: modeStr = "a"; break;
          default:
            currentWorker->ThrowError("Invalid file mode");
            return VariableValue();
        }
        
        File file = SPIFFS.open(path.c_str(), modeStr);
        if (!file) {
          return CreateBooleanVariable(false);
        }
        
        // 创建文件对象
        VMObject* fileObject = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
        fileObject->implement.objectImpl = FileObjectFuncTemplate;
        
        // 分配文件ID
        uint32_t fileId = fileHandleIdSeed++;
        fileObject->implement.objectImpl["_id"] = CreateNumberVariable((double)fileId);
        fileObject->implement.objectImpl["path"] = args[0];
        fileObject->implement.objectImpl["mode"] = CreateNumberVariable((double)mode);
        
        // 存储文件句柄
        fileHandles[fileId] = file;
        
        return CreateReferenceVariable(fileObject);
      });
  
  // 删除文件
  fsClass->implement.objectImpl["remove"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        bool result = SPIFFS.remove(path.c_str());
        return CreateBooleanVariable(result);
      });
  
  // 重命名文件
  fsClass->implement.objectImpl["rename"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING ||
            args[1].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (oldPath, newPath)");
          return VariableValue();
        }
        
        std::string oldPath = args[0].content.ref->implement.stringImpl;
        std::string newPath = args[1].content.ref->implement.stringImpl;
        
        bool result = SPIFFS.rename(oldPath.c_str(), newPath.c_str());
        return CreateBooleanVariable(result);
      });
  
  // 检查文件是否存在
  fsClass->implement.objectImpl["exists"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        bool exists = SPIFFS.exists(path.c_str());
        return CreateBooleanVariable(exists);
      });
  
  // 获取文件信息
  fsClass->implement.objectImpl["stat"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        if (!SPIFFS.exists(path.c_str())) {
          return VariableValue();  // 返回null
        }
        
        File file = SPIFFS.open(path.c_str(), "r");
        if (!file) {
          return VariableValue();  // 返回null
        }
        
        VMObject* infoObj = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
        infoObj->implement.objectImpl["name"] = args[0];
        infoObj->implement.objectImpl["size"] = CreateNumberVariable((double)file.size());
        infoObj->implement.objectImpl["isDirectory"] = CreateBooleanVariable(false);
        infoObj->implement.objectImpl["isFile"] = CreateBooleanVariable(true);
        
        file.close();
        return CreateReferenceVariable(infoObj);
      });
  
  // 列出目录
  fsClass->implement.objectImpl["listDir"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        File root = SPIFFS.open(path.c_str());
        
        if (!root || !root.isDirectory()) {
          if (root) root.close();
          currentWorker->ThrowError("fail to list directory");
          return VariableValue();
        }
        
        VMObject* arrayObj = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::ARRAY);
        
        File file = root.openNextFile();
        while (file) {
        //给每一个文件创建一个info对象，只包含属性
          VMObject* infoObj = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
          infoObj->implement.objectImpl["name"] = CreateReferenceVariable(currentWorker->VMInstance->currentGC->GC_NewStringObject(file.name()));
          infoObj->implement.objectImpl["size"] = CreateNumberVariable((double)file.size());
          infoObj->implement.objectImpl["isDirectory"] = CreateBooleanVariable(file.isDirectory());
          infoObj->implement.objectImpl["isFile"] = CreateBooleanVariable(!file.isDirectory());
          
          arrayObj->implement.arrayImpl.push_back(CreateReferenceVariable(infoObj));
          file = root.openNextFile();
        }
        
        root.close();
        return CreateReferenceVariable(arrayObj);
      });
      // 创建目录（SPIFFS的文件夹是假的，所以为了兼容性模拟一下）
  fsClass->implement.objectImpl["mkdir"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        
        // SPIFFS的mkdir实际上只是标记，不创建实际的目录结构
        // 但可以检查路径格式是否正确
        if (path.empty() || path == "/") {
          return CreateBooleanVariable(true); // 根目录总是存在
        }
        
        // 检查路径中是否包含不支持的字符
        if (path.find("\\") != std::string::npos ||
            path.find(":") != std::string::npos ||
            path.find("*") != std::string::npos ||
            path.find("?") != std::string::npos ||
            path.find("\"") != std::string::npos ||
            path.find("<") != std::string::npos ||
            path.find(">") != std::string::npos ||
            path.find("|") != std::string::npos) {
          currentWorker->ThrowError("Invalid characters in path");
          return VariableValue();
        }
        
        // SPIFFS中mkdir总是成功，因为实际目录结构是虚拟的
        return CreateBooleanVariable(true);
      });
  
  // 删除目录（SPIFFS中，删除目录需要先删除目录下的所有文件）
  fsClass->implement.objectImpl["rmdir"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        
        // 确保路径以/结尾用于目录匹配
        std::string dirPath = path;
        if (!dirPath.empty() && dirPath.back() != '/') {
          dirPath += "/";
        }
        
        // 列出目录下的所有文件
        File root = SPIFFS.open(path.c_str());
        if (!root || !root.isDirectory()) {
          if (root) root.close();
          // 如果不是目录，尝试作为文件删除
          bool result = SPIFFS.remove(path.c_str());
          return CreateBooleanVariable(result);
        }
        
        bool allDeleted = true;
        
        // 递归删除目录下的所有文件
        File file = root.openNextFile();
        while (file) {
          std::string fileName = file.name();
          
          if (file.isDirectory()) {
            // 递归删除子目录
            VariableValue dirArg = CreateReferenceVariable(
                currentWorker->VMInstance->currentGC->GC_NewStringObject(fileName));
            std::vector<VariableValue> rmdirArgs = {dirArg};
            
            //调用自身的rmdir函数递归删除
            auto rmdirFunc = thisValue->implement.objectImpl["rmdir"];
            VariableValue result = rmdirFunc.content.function->funcImpl.system_func(
                rmdirArgs, thisValue, currentWorker);
                
            if (result.getContentType() == ValueType::BOOL && 
                result.content.boolean == false) {
              allDeleted = false;
            }
          } else {
            // 删除文件
            bool deleted = SPIFFS.remove(fileName.c_str());
            if (!deleted) {
              allDeleted = false;
            }
          }
          
          file = root.openNextFile();
        }
        
        root.close();
        return CreateBooleanVariable(allDeleted);
      });
  
  // 删除目录及其所有内容（递归删除）
  fsClass->implement.objectImpl["removeDir"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        // 直接调用rmdir，因为SPIFFS中rmdir就是递归删除
        return thisValue->implement.objectImpl["rmdir"].content.function->funcImpl.system_func(
            args, thisValue, currentWorker);
      });
  
  // 检查是否为目录
  fsClass->implement.objectImpl["isDirectory"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("Invalid argument type: expected (path)");
          return VariableValue();
        }
        
        std::string path = args[0].content.ref->implement.stringImpl;
        
        if (path.empty() || path == "/") {
          return CreateBooleanVariable(true); // 根目录
        }
        
        // 检查路径是否存在文件
        if (SPIFFS.exists(path.c_str())) {
          // 打开路径检查是否是目录
          File file = SPIFFS.open(path.c_str());
          if (file) {
            bool isDir = file.isDirectory();
            file.close();
            return CreateBooleanVariable(isDir);
          }
        }
        
        // 检查是否有以此路径开头的文件（在SPIFFS中表示这是一个目录）
        File root = SPIFFS.open("/");
        if (!root || !root.isDirectory()) {
          if (root) root.close();
          return CreateBooleanVariable(false);
        }
        
        std::string dirPath = path;
        if (!dirPath.empty() && dirPath.back() != '/') {
          dirPath += "/";
        }
        
        bool hasFilesInDir = false;
        File file = root.openNextFile();
        while (file) {
          std::string fileName = file.name();
          // 检查文件路径是否以目录路径开头
          if (fileName.find(dirPath) == 0) {
            hasFilesInDir = true;
            break;
          }
          file = root.openNextFile();
        }
        
        root.close();
        return CreateBooleanVariable(hasFilesInDir);
      });
  
  // 获取总空间
  fsClass->implement.objectImpl["totalBytes"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        return CreateNumberVariable((double)SPIFFS.totalBytes());
      });
  
  // 获取可用空间
  fsClass->implement.objectImpl["usedBytes"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        return CreateNumberVariable((double)SPIFFS.usedBytes());
      });
  
  // 文件模式常量
  fsClass->implement.objectImpl["FILE_READ"] = CreateNumberVariable((double)NEXUS_FILE_READ);
  fsClass->implement.objectImpl["FILE_WRITE"] = CreateNumberVariable((double)NEXUS_FILE_WRITE);
  fsClass->implement.objectImpl["FILE_APPEND"] = CreateNumberVariable((double)NEXUS_FILE_APPEND);
  
  // 注册到全局变量
  std::string fsClassName = "FS";
  auto fsClassRef = CreateReferenceVariable(fsClass);
  VMInstance->storeGlobalSymbol(fsClassName, fsClassRef);
}

#pragma endregion

#endif

