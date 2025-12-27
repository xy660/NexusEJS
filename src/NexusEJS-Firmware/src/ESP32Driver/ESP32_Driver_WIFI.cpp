#ifdef ESP32_PLATFORM

#include <Arduino.h>

#include "BuildinStdlib.h"
#include "ESP32Driver/Esp32Driver.h"
#if ESP32_WEBSERVER_ENABLED
#include <WebServer.h>
#endif
#if ESP32_SOCKET_ENABLED
#include <WiFiClient.h>
#endif
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>

#include "GC.h"
#include "StringConverter.h"
#include "VM.h"

#if ESP32_WIFI_API_ENABLED

#if ESP32_WEBSERVER_ENABLED
// 用于处理脚本HTTP处理程序回调的返回值，转换并发回请求方

void __ESP32_HTTP_SendBufferResp(uint32_t bufid, WebServer* pServer,
                                 std::string type, int code) {
  auto info = GetByteBufferInfo(bufid);
  printf("bufid=%d info.data=%p info.length=%d\n", bufid, info.data,
         info.length);
  pServer->send_P(code, type.c_str(), (const char*)info.data, info.length);
}

void __ESP32_HTTP_SendResponse(VariableValue& result, WebServer* pServer) {
  switch (result.getContentType()) {
    case ValueType::OBJECT:  // 详情信息啊
    {
      // 后续更新ButeBuffer了需要在这里判断
      //{content,code,type}
      auto& obj = result.content.ref->implement.objectImpl;
      if (obj.find("bufid") != obj.end()) {  // 判断是不是Buffer对象
        uint32_t bufid = (uint32_t)obj["bufid"].content.number;
        __ESP32_HTTP_SendBufferResp(bufid, pServer, "application/octet-stream",
                                    200);
      } else {
        int code = obj["code"].getContentType() == ValueType::NUM
                       ? (int)obj["code"].content.number
                       : 200;
        std::string type = obj["type"].ToString();
        auto contentValue = obj["content"];
        // 判断是不是Buffer对象
        if (contentValue.getContentType() == ValueType::OBJECT &&
            contentValue.content.ref->implement.objectImpl.find("bufid") !=
                contentValue.content.ref->implement.objectImpl.end()) {
          uint32_t bufid =
              (uint32_t)contentValue.content.ref->implement.objectImpl["bufid"]
                  .content.number;
          __ESP32_HTTP_SendBufferResp(bufid, pServer, type, code);

        } else {  // 序列化返回
          std::string content = contentValue.ToString();
          pServer->send(code, type.c_str(), content.c_str());
        }
      }
      break;
    }
    default:  // 其他的都toString后返回，懒得判断了，后续ByteBuffer就在顶上判断
    {
      std::string send = result.ToString();
      pServer->send(200, "text/plain", send.c_str());
      break;
    }
  }
}

#endif

#if ESP32_HTTPCLIENT_ENABLED
std::unordered_map<std::string, VariableValue> HTTPClientRespObjectTemplate;
void HTTPClientRespObjectTempInit() {
  // text() 方法
  HTTPClientRespObjectTemplate["text"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisVal,
         VMWorker* w) -> VariableValue {
        auto it = thisVal->implement.objectImpl.find("_buf");
        if (it != thisVal->implement.objectImpl.end()) {
          uint32_t bufid = (uint32_t)(*it).second.content.ref->implement.objectImpl["bufid"].content.number;
          auto bufinfo = GetByteBufferInfo(bufid);
          std::string str((const char*)bufinfo.data,bufinfo.length);
          return CreateReferenceVariable(w->VMInstance->currentGC->GC_NewStringObject(str));
        }
        return VariableValue();
      });

  // buffer() 方法
  HTTPClientRespObjectTemplate["buffer"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisVal,
         VMWorker* w) -> VariableValue {
        auto it = thisVal->implement.objectImpl.find("_buf");
        if (it != thisVal->implement.objectImpl.end()) {
          return it->second; //返回buffer对象本身
        }
        return VariableValue();
      });
}

#endif

#if ESP32_WEBSERVER_ENABLED

static std::unordered_map<uint32_t, std::unique_ptr<WebServer>>
    webserverInstances;  // 定义全局单例绑定表
static std::unordered_map<uint32_t, TaskHandle_t>
    webserverLoopTasks;  // 独立运行web服务循环的RTOS任务
static uint32_t webserverInstanceIdSeed = 1;

std::unordered_map<std::string, VariableValue> WebServerObjectTemplate;

void WebServerObjectTempInit() {
  // 创建析构函数销毁绑定的原生web服务对象
  WebServerObjectTemplate["finalize"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        // 销毁相关原生资源
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        // 检查并停止RTOS任务
        if (webserverLoopTasks.find(nativeId) != webserverLoopTasks.end()) {
          vTaskDelete(webserverLoopTasks[nativeId]);
          webserverLoopTasks.erase(nativeId);
        }
        printf("WebServer.finalize: single map size=%d\n",
               webserverInstances.size());
        webserverInstances.erase(nativeId);
        printf("WebServer.finalize: erased single map size=%d\n",
               webserverInstances.size());
        return CreateBooleanVariable(true);  // 允许GC回收对象
      });

      //主动释放函数销毁绑定的原生web服务对象
  WebServerObjectTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        // 销毁相关原生资源
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        // 检查并停止RTOS任务
        if (webserverLoopTasks.find(nativeId) != webserverLoopTasks.end()) {
          vTaskDelete(webserverLoopTasks[nativeId]);
          webserverLoopTasks.erase(nativeId);
        }
        printf("WebServer.finalize: single map size=%d\n",
               webserverInstances.size());
        webserverInstances.erase(nativeId);
        printf("WebServer.finalize: erased single map size=%d\n",
               webserverInstances.size());
          //销毁析构
        thisValue->implement.objectImpl.erase("finalize");
        return VariableValue();
      });

  // 准备操作方法
  WebServerObjectTemplate["begin"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto pServer = webserverInstances[nativeId].get();
        pServer->begin();
        TaskHandle_t serverLoopHandle = NULL;
        xTaskCreate(
            [](void* hArg) {
              uint32_t nativeId = (uint32_t)hArg;
              WebServer* web = webserverInstances[nativeId].get();
              while (true) {
                web->handleClient();
              }
            },
            "WebProcess", 6 * 1024, (void*)nativeId, 1, &serverLoopHandle);
        if (serverLoopHandle) {
          webserverLoopTasks[nativeId] = serverLoopHandle;
        } else {
          currentWorker->ThrowError("start server failed");
        }
        return VariableValue();
      });
  // mapxx(url,callback)
  WebServerObjectTemplate["mapGet"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("url must be a string");
          return VariableValue();
        }
        if (args[1].getContentType() != ValueType::FUNCTION &&
            args[1].content.function->type != ScriptFunction::Local &&
            args[1].content.function->funcImpl.local_func.arguments.size() !=
                1) {
          currentWorker->ThrowError("invaild callback");
          return VariableValue();
        }

        // 将回调对象存入this._cb中，防止GC误回收
        thisValue->implement.objectImpl["_cb"]
            .content.ref->implement.arrayImpl.push_back(args[1]);
        VariableValue callback = args[1];  // 拷贝持有一份闭包

        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto pServer = webserverInstances[nativeId].get();
        VM* currentVM = currentWorker->VMInstance;
        // 实现回调层
        pServer->on(
            args[0].content.ref->implement.stringImpl.c_str(),
            [nativeId, currentVM, callback]() {
              VariableValue _callback = callback;
              auto pServer = webserverInstances[nativeId].get();
              // 不从GC分配，减轻GC压力和内存碎片
              VMObject argObject(ValueType::OBJECT);
              std::vector<VMObject*> paramObjects;
              paramObjects.reserve(
                  pServer->args());  // 预留参数空位避免扩容导致指针失效
              auto& aobjmap = argObject.implement.objectImpl;
              // 拷贝参数过去
              for (int i = 0; i < pServer->args(); i++) {
                std::string argName = pServer->argName(i).c_str();
                std::string param = pServer->arg(i).c_str();
                // paramObjects.emplace_back(ValueType::STRING);
                // paramObjects.back().implement.stringImpl = w_param;
                VMObject* vmo = new VMObject(
                    ValueType::
                        STRING);  // 创建临时的字符串对象，回调返回后需要遍历delete
                vmo->implement.stringImpl = param;
                paramObjects.push_back(vmo);
                aobjmap[argName] = CreateReferenceVariable(vmo);
              }
              // 在新的执行环境执行回调
              // VMWorker worker(currentVM);

              std::vector<VariableValue> argument;
              argument.push_back(CreateReferenceVariable(&argObject));
              // auto result =
              // worker->Init(callback->funcImpl.local_func,&argument);
              // //当前线程运行解释器循环
              auto result =
                  currentVM->InvokeCallback(_callback, argument, NULL);
              // 发送返回值作为response

              __ESP32_HTTP_SendResponse(result, pServer);

              // 清理临时对象
              for (VMObject* paramobj : paramObjects) {
                delete paramobj;
              }
            });
        return VariableValue();
      });
  WebServerObjectTemplate["mapPost"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("url must be a string");
          return VariableValue();
        }
        if (args[1].getContentType() != ValueType::FUNCTION &&
            args[1].content.function->type != ScriptFunction::Local &&
            args[1].content.function->funcImpl.local_func.arguments.size() !=
                1) {
          currentWorker->ThrowError("invaild callback");
          return VariableValue();
        }
        // 将回调对象存入this._cb中，防止GC误回收
        thisValue->implement.objectImpl["_cb"]
            .content.ref->implement.arrayImpl.push_back(args[1]);
        VariableValue callback = args[1];  // 拷贝持有一份闭包

        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
        auto pServer = webserverInstances[nativeId].get();
        VM* currentVM = currentWorker->VMInstance;
        // 实现回调层
        pServer->on(
            args[0].content.ref->implement.stringImpl.c_str(), HTTP_POST,
            [nativeId, currentVM, callback]() {
              VariableValue _callback = callback;
              auto pServer = webserverInstances[nativeId].get();
              // 不从GC分配，减轻GC压力和内存碎片
              VMObject argObject(ValueType::OBJECT);
              std::vector<VMObject*> paramObjects;
              paramObjects.reserve(
                  pServer->args());  // 预留参数空位避免扩容导致指针失效
              auto& aobjmap = argObject.implement.objectImpl;
              // 拷贝参数过去
              for (int i = 0; i < pServer->args(); i++) {
                // 转换成UTF16给引擎回调
                std::string argName = pServer->argName(i).c_str();
                std::string param = pServer->arg(i).c_str();
                // paramObjects.emplace_back(ValueType::STRING);
                // paramObjects.back().implement.stringImpl = w_param;
                VMObject* vmo = new VMObject(
                    ValueType::
                        STRING);  // 创建临时的字符串对象，回调返回后需要遍历delete
                vmo->implement.stringImpl = param;
                paramObjects.push_back(vmo);
                aobjmap[argName] = CreateReferenceVariable(vmo);
              }
              // 在新的执行环境执行回调
              // VMWorker worker(currentVM);

              std::vector<VariableValue> argument;
              argument.push_back(CreateReferenceVariable(&argObject));
              // auto result =
              // worker->Init(callback->funcImpl.local_func,&argument);
              // //当前线程运行解释器循环
              auto result =
                  currentVM->InvokeCallback(_callback, argument, NULL);
              // 发送返回值作为response

              __ESP32_HTTP_SendResponse(result, pServer);

              // 清理临时对象
              for (VMObject* paramobj : paramObjects) {
                delete paramobj;
              }
            });
        return VariableValue();
      });
}

#endif

void ESP32_HTTPApi_Init(VM* VMInstance) {
#if ESP32_HTTPCLIENT_ENABLED || ESP32_WEBSERVER_ENABLED
  VMObject* httpClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
#endif

#if ESP32_HTTPCLIENT_ENABLED

  HTTPClientRespObjectTempInit();
  httpClass->implement.objectImpl["fetch"] = VM::CreateSystemFunc(
      DYNAMIC_ARGUMENT,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args.empty() || args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("fetch(url, [options])");
          return VariableValue();
        }

        String url = String(args[0].content.ref->implement.stringImpl.c_str());
        String method = "GET";
        String body = "";

        if (args.size() > 1 && args[1].getContentType() == ValueType::OBJECT) {
          VMObject* options = args[1].content.ref;

          auto it_method = options->implement.objectImpl.find("method");
          if (it_method != options->implement.objectImpl.end() &&
              it_method->second.getContentType() == ValueType::STRING) {
            method = String(
                it_method->second.content.ref->implement.stringImpl.c_str());
            method.toUpperCase();
          }

          auto it_body = options->implement.objectImpl.find("body");
          if (it_body != options->implement.objectImpl.end() &&
              it_body->second.getContentType() == ValueType::STRING) {
            body = String(
                it_body->second.content.ref->implement.stringImpl.c_str());
          }
        }

        HTTPClient http;
        if (!http.begin(url)) {
          currentWorker->ThrowError("HTTPClient.begin failed");
          return VariableValue();
        }

        int httpCode = 0;
        if (method == "GET")
          httpCode = http.GET();
        else if (method == "POST") {
          http.addHeader("Content-Type", "application/json");
          httpCode = http.POST(body);
        } else if (method == "PUT") {
          http.addHeader("Content-Type", "application/json");
          httpCode = http.PUT(body);
        } else if (method == "DELETE")
          httpCode = http.sendRequest("DELETE");
        else
          httpCode = http.GET();

        VMObject* bufferObject;
        if (httpCode > 0) {
          bufferObject = CreateByteBufferObject(http.getSize(),currentWorker);
          uint32_t bufid = (uint32_t)bufferObject->implement.objectImpl["bufid"].content.number;
          auto info = GetByteBufferInfo(bufid);
          uint32_t offset = 0;
          while(offset < info.length && http.getStream().available()){
            offset += http.getStream().readBytes(info.data + offset,info.length - offset);
          }
        }
        else{
          http.end();
          currentWorker->ThrowError("the request is failed");
          return VariableValue(); //fix: 这里修复一个严重错误，请求失败会导致Buffer空指针，现在改为抛出异常
        }
        http.end();

        // 创建响应对象
        VMObject* response = currentWorker->VMInstance->currentGC->GC_NewObject(
            ValueType::OBJECT);

        response->implement.objectImpl =
            HTTPClientRespObjectTemplate;  // 拷贝响应对象模板

        // 基本属性
        bool ok = httpCode >= 200 && httpCode < 300;
        response->implement.objectImpl["ok"] = CreateBooleanVariable(ok);
        response->implement.objectImpl["status"] =
            CreateNumberVariable(httpCode);

        // 状态文本
        VMObject* statusTextObj =
            currentWorker->VMInstance->currentGC->GC_NewStringObject(
                String(httpCode).c_str());
        response->implement.objectImpl["statusText"] =
            CreateReferenceVariable(statusTextObj);

        // URL
        response->implement.objectImpl["url"] = args[0];

        // 内部body存储
        response->implement.objectImpl["_buf"] =
            CreateReferenceVariable(bufferObject);

        return CreateReferenceVariable(response);
      });
#endif

#if ESP32_WEBSERVER_ENABLED

  WebServerObjectTempInit();
  httpClass->implement.objectImpl["createServer"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("port must be number");
          return VariableValue();
        }

        uint32_t port = (uint32_t)args[0].content.number;

        VMObject* serverObject =
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);

        serverObject->implement.objectImpl =
            WebServerObjectTemplate;  // 拷贝对象模板

        auto& objContainer = serverObject->implement.objectImpl;
        uint32_t serverId = webserverInstanceIdSeed++;

        WebServer* nativeWebServer = new WebServer(80);
        webserverInstances[serverId] =
            std::unique_ptr<WebServer>(nativeWebServer);
        objContainer["_id"] = CreateNumberVariable((double)serverId);
        objContainer["port"] = CreateNumberVariable(args[0].content.number);
        objContainer["_cb"] = CreateReferenceVariable(
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::ARRAY));

        return CreateReferenceVariable(serverObject);
      });
#endif

#if ESP32_HTTPCLIENT_ENABLED || ESP32_WEBSERVER_ENABLED
  std::string httpClassName = "Http";
  auto httpClassRef = CreateReferenceVariable(httpClass);
  VMInstance->storeGlobalSymbol(httpClassName, httpClassRef);
#endif
}

#if ESP32_SOCKET_ENABLED

std::unordered_map<std::string, VariableValue> TCPSocketObjectTemplate;
static std::unordered_map<uint32_t, std::unique_ptr<WiFiClient>>
    tcpclientInstances;
static uint32_t tcpclientInstanceIdSeed = 1;

bool receiveExactSize(WiFiClient& client, uint8_t* buffer, size_t size) {
  size_t received = 0;

  while (received < size) {
    if (client.available()) {
      int len = client.read(buffer + received, size - received);
      if (len > 0) {
        received += len;
      }
    }

    delay(1);
  }
  return true;
}

void TCPSocketObjectTempInit() {
  // 创建析构函数
  TCPSocketObjectTemplate["finalize"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (tcpclientInstances.find(nativeId) != tcpclientInstances.end()) {
          // 直接释放资源，不需要检查连接状态
          tcpclientInstances.erase(nativeId);
        }

        return CreateBooleanVariable(true);
      });

    TCPSocketObjectTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (tcpclientInstances.find(nativeId) != tcpclientInstances.end()) {
          // 直接释放资源，不需要检查连接状态
          tcpclientInstances.erase(nativeId);
        }
        thisValue->implement.objectImpl.erase("finalize");
        return CreateBooleanVariable(true);
      });

  // 发送数据 (buf,offset,length)
  TCPSocketObjectTemplate["send"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[2].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("invaild argument type");
          return VariableValue();
        }
        uint32_t offset = (uint32_t)args[1].content.number;
        uint32_t length = (uint32_t)args[2].content.number;
        if (args[0].getContentType() == ValueType::OBJECT) {
          auto& obj = args[0].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data) {
              currentWorker->ThrowError("invaild Buffer");
              return VariableValue();
            }
            uint32_t nativeId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            if (tcpclientInstances.find(nativeId) == tcpclientInstances.end()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            WiFiClient* client = tcpclientInstances[nativeId].get();
            if (!client->connected()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            if (offset >= bufinfo.length || offset + length > bufinfo.length ||
                length <= 0) {
              currentWorker->ThrowError("out of range");
              return VariableValue();
            }
            client->write_P((const char*)(bufinfo.data + offset), length);
            return VariableValue();
          }
        }
        currentWorker->ThrowError("send only apply Buffer");
        return VariableValue();
      });

  // recv(buf,offset,buf_length) : number 返回值表示收到的大小
  TCPSocketObjectTemplate["recv"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[2].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("invaild argument type");
          return VariableValue();
        }
        uint32_t offset = (uint32_t)args[1].content.number;
        uint32_t length = (uint32_t)args[2].content.number;
        if (args[0].getContentType() == ValueType::OBJECT) {
          auto& obj = args[0].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data) {
              currentWorker->ThrowError("invaild Buffer");
              return VariableValue();
            }
            uint32_t nativeId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            if (tcpclientInstances.find(nativeId) == tcpclientInstances.end()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            WiFiClient* client = tcpclientInstances[nativeId].get();
            if (!client->connected()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            if (offset >= bufinfo.length || offset + length > bufinfo.length ||
                length <= 0) {
              currentWorker->ThrowError("out of range");
              return VariableValue();
            }
            uint32_t ret = client->readBytes(bufinfo.data + offset, length);
            return CreateNumberVariable(ret);
          }
        }
        currentWorker->ThrowError("send only apply Buffer");
        return VariableValue();
      });
  // recvSize(buf,offset,buf_length) : boolean
  TCPSocketObjectTemplate["recvSize"] = VM::CreateSystemFunc(
      3,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[2].getContentType() != ValueType::NUM ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("invaild argument type");
          return VariableValue();
        }
        uint32_t offset = (uint32_t)args[1].content.number;
        uint32_t length = (uint32_t)args[2].content.number;
        if (args[0].getContentType() == ValueType::OBJECT) {
          auto& obj = args[0].content.ref->implement.objectImpl;
          if (obj.find("bufid") != obj.end()) {
            uint32_t bufid = obj["bufid"].content.number;
            auto bufinfo = GetByteBufferInfo(bufid);
            if (!bufinfo.data) {
              currentWorker->ThrowError("invaild Buffer");
              return VariableValue();
            }
            uint32_t nativeId =
                (uint32_t)thisValue->implement.objectImpl["_id"].content.number;
            if (tcpclientInstances.find(nativeId) == tcpclientInstances.end()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            WiFiClient* client = tcpclientInstances[nativeId].get();
            if (!client->connected()) {
              currentWorker->ThrowError("connection has closed");
              return VariableValue();
            }
            if (offset >= bufinfo.length || offset + length > bufinfo.length ||
                length <= 0) {
              currentWorker->ThrowError("out of range");
              return VariableValue();
            }
            bool ret = receiveExactSize(*client, bufinfo.data, bufinfo.length);
            return CreateBooleanVariable(ret);
          }
        }
        currentWorker->ThrowError("send only apply Buffer");
        return VariableValue();
      });

  // 检查连接状态
  TCPSocketObjectTemplate["isConnected"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (tcpclientInstances.find(nativeId) == tcpclientInstances.end()) {
          return CreateBooleanVariable(false);
        }

        WiFiClient* client = tcpclientInstances[nativeId].get();
        bool isConnected = client->connected();

        if (!isConnected) {
          tcpclientInstances.erase(nativeId);
        }

        return CreateBooleanVariable(isConnected);
      });

  // 断开连接
  TCPSocketObjectTemplate["close"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (tcpclientInstances.find(nativeId) != tcpclientInstances.end()) {
          tcpclientInstances.erase(nativeId);
        }

        return CreateBooleanVariable(true);
      });

  // 设置超时
  TCPSocketObjectTemplate["setTimeout"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("timeout must be a number");
          return VariableValue();
        }

        uint32_t nativeId =
            (uint32_t)thisValue->implement.objectImpl["_id"].content.number;

        if (tcpclientInstances.find(nativeId) != tcpclientInstances.end()) {
          tcpclientInstances[nativeId]->setTimeout((int)args[0].content.number);
          return CreateBooleanVariable(true);
        }

        return CreateBooleanVariable(false);
      });
}

void ESP32_SocketApi_Init(VM* VMInstance) {
  TCPSocketObjectTempInit();
  VMObject* socketClass =
      VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
  socketClass->implement.objectImpl["connectTCP"] = VM::CreateSystemFunc(
      2,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING ||
            args[1].getContentType() != ValueType::NUM) {
          currentWorker->ThrowError("invaild argument type");
          return VariableValue();
        }

        std::string ipstr = args[0].content.ref->implement.stringImpl;
        ;
        uint16_t port = (uint16_t)args[1].content.number;

        IPAddress ipAddr;
        if (!ipAddr.fromString(ipstr.c_str())) {
          currentWorker->ThrowError("invalid IP address format");
          return VariableValue();
        }

        // 先尝试连接，成功后再创建VM对象
        WiFiClient* nativeClient = new WiFiClient();
        bool connected = nativeClient->connect(ipAddr, port);

        if (!connected) {
          delete nativeClient;  // 连接失败，立即释放内存
          currentWorker->ThrowError("connection failed");
          return VariableValue();
        }

        // 连接成功，创建VM对象
        uint32_t clientId = tcpclientInstanceIdSeed++;
        tcpclientInstances[clientId] =
            std::unique_ptr<WiFiClient>(nativeClient);

        VMObject* socketObject =
            currentWorker->VMInstance->currentGC->GC_NewObject(
                ValueType::OBJECT);

        socketObject->implement.objectImpl =
            TCPSocketObjectTemplate;  // 拷贝对象模板

        auto& objContainer = socketObject->implement.objectImpl;

        objContainer["_id"] = CreateNumberVariable((double)clientId);
        objContainer["ip"] = args[0];
        objContainer["port"] = CreateNumberVariable(args[1].content.number);
        objContainer["connected"] = CreateBooleanVariable(true);

        return CreateReferenceVariable(socketObject);
      });

  std::string socketClassName = "Socket";
  auto socketClassRef = CreateReferenceVariable(socketClass);
  VMInstance->storeGlobalSymbol(socketClassName, socketClassRef);
}

#endif

void ESP32_NetworkApi_Init(VM* VMInstance) {
#if ESP32_WEBSERVER_ENABLED
  ESP32_HTTPApi_Init(VMInstance);
#endif
#if ESP32_SOCKET_ENABLED
  ESP32_SocketApi_Init(VMInstance);
#endif
}

void ESP32_WifiHardwareApi_Init(VM* VMInstance) {
  VMObject* wifiClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
  wifiClass->implement.objectImpl["softAP"] = VM::CreateSystemFunc(
      DYNAMIC_ARGUMENT,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        // WiFi.softAP(ssid);
        if (args.size() == 1 && args[0].getContentType() == ValueType::STRING) {
          std::string ssid = args[0].content.ref->implement.stringImpl;
          bool result = WiFi.softAP(ssid.c_str());
          return CreateBooleanVariable(result);
        }  // WiFi.softAP(ssid,pass);
        else if (args.size() == 2 &&
                 args[0].getContentType() == ValueType::STRING &&
                 args[1].getContentType() == ValueType::STRING) {
          std::string ssid = args[0].content.ref->implement.stringImpl;
          std::string password = args[1].content.ref->implement.stringImpl;
          bool result = WiFi.softAP(ssid.c_str(), password.c_str());
          return CreateBooleanVariable(result);
        } else {
          currentWorker->ThrowError("softAP() invaild argument");
          return VariableValue();
        }
      });
  wifiClass->implement.objectImpl["connect"] = VM::CreateSystemFunc(
      DYNAMIC_ARGUMENT,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        // WiFi.connect(ssid);
        if (args.size() == 1 && args[0].getContentType() == ValueType::STRING) {
          std::string ssid = args[0].content.ref->implement.stringImpl;
          bool result = WiFi.begin(ssid.c_str());
          return CreateBooleanVariable(result);
        }  // WiFi.connect(ssid,pass);
        else if (args.size() == 2 &&
                 args[0].getContentType() == ValueType::STRING &&
                 args[1].getContentType() == ValueType::STRING) {
          std::string ssid = args[0].content.ref->implement.stringImpl;
          std::string password = args[1].content.ref->implement.stringImpl;
          bool result = WiFi.begin(ssid.c_str(), password.c_str());
          return CreateBooleanVariable(result);
        } else {
          currentWorker->ThrowError("WiFi.connect() invaild argument");
          return VariableValue();
        }
      });
  wifiClass->implement.objectImpl["isConnected"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        return CreateBooleanVariable(WiFi.status() == WL_CONNECTED);
      });
  wifiClass->implement.objectImpl["setMode"] = VM::CreateSystemFunc(
      1,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        if (args[0].getContentType() != ValueType::STRING) {
          currentWorker->ThrowError("setMode invaild argument");
        }
        auto& mode = args[0].content.ref->implement.stringImpl;
        if (mode == "ap") {
          WiFi.mode(WIFI_MODE_AP);
        } else if (mode == "sta") {
          WiFi.mode(WIFI_MODE_STA);
        } else if (mode == "ap_sta") {
          WiFi.mode(WIFI_MODE_APSTA);
        } else if (mode == "none") {
          WiFi.mode(WIFI_MODE_NULL);
        } else {
          currentWorker->ThrowError("setMode unknown command " + mode);
        }

        return VariableValue();
      });

  // WiFi.localIP
  wifiClass->implement.objectImpl["localIP"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        IPAddress ip = WiFi.localIP();
        String ipStr = String(ip[0]) + "." + String(ip[1]) + "." +
                       String(ip[2]) + "." + String(ip[3]);
        VMObject* strObj =
            currentWorker->VMInstance->currentGC->GC_NewStringObject(
                ipStr.c_str());
        return CreateReferenceVariable(strObj);
      });

  // WiFi.SSID
  wifiClass->implement.objectImpl["SSID"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        String ssid = WiFi.SSID();
        VMObject* strObj =
            currentWorker->VMInstance->currentGC->GC_NewStringObject(
                ssid.c_str());
        return CreateReferenceVariable(strObj);
      });

  // WiFi.RSSI
  wifiClass->implement.objectImpl["RSSI"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        int32_t rssi = WiFi.RSSI();
        return CreateNumberVariable(rssi);
      });

  // WiFi.macAddress
  wifiClass->implement.objectImpl["macAddress"] = VM::CreateSystemFunc(
      0,
      [](std::vector<VariableValue>& args, VMObject* thisValue,
         VMWorker* currentWorker) -> VariableValue {
        String mac = WiFi.macAddress();
        VMObject* strObj =
            currentWorker->VMInstance->currentGC->GC_NewStringObject(
                mac.c_str());
        return CreateReferenceVariable(strObj);
      });

  std::string className = "WiFi";
  auto ref = CreateReferenceVariable(wifiClass);
  VMInstance->storeGlobalSymbol(className, ref);
}

void ESP32_WiFiPlatformApi_Init(VM* VMInstance) {
  ESP32_WifiHardwareApi_Init(VMInstance);
  ESP32_NetworkApi_Init(VMInstance);
}

#endif

#endif