#ifdef ESP32_PLATFORM

#include "ESP32Driver/Esp32Driver.h"
#include "BuildinStdlib.h"
#include <Arduino.h>
#if ESP32_WEBSERVER_ENABLED
#include <WebServer.h>
#endif
#if ESP32_SOCKET_ENABLED
#include <WiFiClient.h>
#endif
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiServer.h>
#include <WiFiClient.h>
#include "VM.h"
#include "GC.h"
#include "StringConverter.h"


#if ESP32_WIFI_API_ENABLED

#if ESP32_WEBSERVER_ENABLED
//用于处理脚本HTTP处理程序回调的返回值，转换并发回请求方


void __ESP32_HTTP_SendBufferResp(uint32_t bufid,WebServer* pServer,std::string type,int code){
    auto info = GetByteBufferInfo(bufid);
    printf("bufid=%d info.data=%p info.length=%d\n",bufid,info.data,info.length);
    pServer->send_P(code,type.c_str(),(const char*)info.data,info.length);
}

void __ESP32_HTTP_SendResponse(VariableValue& result,WebServer* pServer){
    switch (result.getContentType())
    {
    case ValueType::OBJECT: //详情信息啊
    {
        //后续更新ButeBuffer了需要在这里判断
        //{content,code,type}
        auto& obj = result.content.ref->implement.objectImpl;
        if(obj.find(L"bufid") != obj.end()){ //判断是不是Buffer对象
            uint32_t bufid = (uint32_t)obj[L"bufid"].content.number;
            __ESP32_HTTP_SendBufferResp(bufid,pServer,"application/octet-stream",200);
        }
        else{
            int code = obj[L"code"].getContentType() == ValueType::NUM ? (int)obj[L"code"].content.number : 200;
            std::string type = wstring_to_string(obj[L"type"].ToString());
            auto contentValue = obj[L"content"];
            //判断是不是Buffer对象
            if(contentValue.getContentType() == ValueType::OBJECT &&
            contentValue.content.ref->implement.objectImpl.find(L"bufid") != contentValue.content.ref->implement.objectImpl.end()){
                
                uint32_t bufid = (uint32_t)contentValue.content.ref->implement.objectImpl[L"bufid"].content.number;
                __ESP32_HTTP_SendBufferResp(bufid,pServer,type,code);

            }else{ //序列化返回
                std::string content = wstring_to_string(contentValue.ToString());
                pServer->send(code,type.c_str(),content.c_str());
            }
        }
        break;
    }
    default: //其他的都toString后返回，懒得判断了，后续ByteBuffer就在顶上判断
    {
        std::string send = wstring_to_string(result.ToString());
        pServer->send(200,"text/plain",send.c_str());
        break;
    }
    }

}

#endif

void ESP32_HTTPApi_Init(VM* VMInstance){
#if ESP32_HTTPCLIENT_ENABLED || ESP32_WEBSERVER_ENABLED
    VMObject* httpClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
#endif

#if ESP32_HTTPCLIENT_ENABLED
    
    httpClass->implement.objectImpl[L"fetch"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if (args.empty() || args[0].getContentType() != ValueType::STRING) {
            currentWorker->ThrowError(L"fetch(url, [options])");
            return VariableValue();
        }
        
        String url = String(wstring_to_string(args[0].content.ref->implement.stringImpl).c_str());
        String method = "GET";
        String body = "";
        
        if (args.size() > 1 && args[1].getContentType() == ValueType::OBJECT) {
            VMObject* options = args[1].content.ref;
            
            auto it_method = options->implement.objectImpl.find(L"method");
            if (it_method != options->implement.objectImpl.end() && 
                it_method->second.getContentType() == ValueType::STRING) {
                method = String(wstring_to_string(it_method->second.content.ref->implement.stringImpl).c_str());
                method.toUpperCase();
            }
            
            auto it_body = options->implement.objectImpl.find(L"body");
            if (it_body != options->implement.objectImpl.end() && 
                it_body->second.getContentType() == ValueType::STRING) {
                body = String(wstring_to_string(it_body->second.content.ref->implement.stringImpl).c_str());
            }
        }
        
        HTTPClient http;
        if (!http.begin(url)) {
            currentWorker->ThrowError(L"HTTPClient.begin failed");
            return VariableValue();
        }
        
        int httpCode = 0;
        if (method == "GET") httpCode = http.GET();
        else if (method == "POST") {
            http.addHeader("Content-Type", "application/json");
            httpCode = http.POST(body);
        }
        else if (method == "PUT") {
            http.addHeader("Content-Type", "application/json");
            httpCode = http.PUT(body);
        }
        else if (method == "DELETE") httpCode = http.sendRequest("DELETE");
        else httpCode = http.GET();
        
        String responseBody = "";
        if (httpCode > 0) {
            responseBody = http.getString();
        }
        http.end();
        
        // 创建响应对象
        VMObject* response = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
        
        // 基本属性
        bool ok = httpCode >= 200 && httpCode < 300;
        response->implement.objectImpl[L"ok"] = CreateBooleanVariable(ok);
        response->implement.objectImpl[L"status"] = CreateNumberVariable(httpCode);
        
        // 状态文本
        VMObject* statusTextObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(
            string_to_wstring(String(httpCode).c_str()));
        response->implement.objectImpl[L"statusText"] = CreateReferenceVariable(statusTextObj);
        
        // URL
        response->implement.objectImpl[L"url"] = args[0];
        
        // 内部body存储
        VMObject* bodyObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(
            string_to_wstring(responseBody.c_str()));
        response->implement.objectImpl[L"_body"] = CreateReferenceVariable(bodyObj);
        
        // text() 方法
        response->implement.objectImpl[L"text"] = VM::CreateSystemFunc(0,
            [](std::vector<VariableValue>& args, VMObject* thisVal, VMWorker* w) -> VariableValue {
                auto it = thisVal->implement.objectImpl.find(L"_body");
                if (it != thisVal->implement.objectImpl.end()) {
                    return it->second;
                }
                return VariableValue();
            });
        
        // buffer() 方法
        response->implement.objectImpl[L"buffer"] = VM::CreateSystemFunc(0,
            [](std::vector<VariableValue>& args, VMObject* thisVal, VMWorker* w) -> VariableValue {
                w->ThrowError(L".buffer() not implement on this version");
                return VariableValue();
                auto it = thisVal->implement.objectImpl.find(L"_body");
                if (it != thisVal->implement.objectImpl.end()) {
                    return it->second;
                }
                return VariableValue();
            });
        
        return CreateReferenceVariable(response);
    });
#endif

#if ESP32_WEBSERVER_ENABLED
    httpClass->implement.objectImpl[L"createServer"] = VM::CreateSystemFunc(1,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if(args[0].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"port must be number");
            return VariableValue();
        }
        static std::unordered_map<uint32_t,std::unique_ptr<WebServer>> serverInstances; // 定义全局单例绑定表
        static std::unordered_map<uint32_t,TaskHandle_t> serverLoopTasks; //独立运行web服务循环的RTOS任务
        static uint32_t serverInstanceIdSeed = 1;

        uint32_t port = (uint32_t)args[0].content.number;

        VMObject* serverObject = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
        auto& objContainer = serverObject->implement.objectImpl;
        uint32_t serverId = serverInstanceIdSeed++;

        WebServer* nativeWebServer = new WebServer(80);
        serverInstances[serverId] = std::unique_ptr<WebServer>(nativeWebServer);
        objContainer[L"_id"] = CreateNumberVariable((double)serverId);
        objContainer[L"port"] = CreateNumberVariable(args[0].content.number);
        //创建析构函数销毁绑定的原生web服务对象
        objContainer[L"finalize"] = VM::CreateSystemFunc(0,[](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue{
            //销毁相关原生资源
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            //检查并停止RTOS任务
            if(serverLoopTasks.find(nativeId) != serverLoopTasks.end()){
                vTaskDelete(serverLoopTasks[nativeId]); 
                serverLoopTasks.erase(nativeId);
            }
            printf("WebServer.finalize: single map size=%d\n",serverInstances.size()); 
            serverInstances.erase(nativeId);
            printf("WebServer.finalize: erased single map size=%d\n",serverInstances.size()); 
            return CreateBooleanVariable(true); //允许GC回收对象
        });
        //准备操作方法
        objContainer[L"begin"] = VM::CreateSystemFunc(0,[](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue{
            
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            auto pServer = serverInstances[nativeId].get();
            pServer->begin();
            TaskHandle_t serverLoopHandle = NULL;
            xTaskCreate([](void* hArg){
                uint32_t nativeId = (uint32_t)hArg;
                WebServer* web = serverInstances[nativeId].get();
                while(true){
                    web->handleClient();
                }
            },"webProcess",6 * 1024,(void*)nativeId,1,&serverLoopHandle);
            if(serverLoopHandle){
                serverLoopTasks[nativeId] = serverLoopHandle;
            }else{
                currentWorker->ThrowError(L"start server failed");
            }
            return VariableValue();
        });
        //mapxx(url,callback)
        objContainer[L"mapGet"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue{
            if(args[0].getContentType() != ValueType::STRING){
                currentWorker->ThrowError(L"url must be a string");
                return VariableValue();
            }
            if(args[1].getContentType() != ValueType::FUNCTION &&
                args[1].content.function->type != ScriptFunction::Local &&
                args[1].content.function->funcImpl.local_func.arguments.size() != 1){
                currentWorker->ThrowError(L"invaild callback");
                return VariableValue();
            }
            ScriptFunction* callback = args[1].content.function;

            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            auto pServer = serverInstances[nativeId].get();
            VM* currentVM = currentWorker->VMInstance;
            //实现回调层
            pServer->on(wstring_to_string(args[0].content.ref->implement.stringImpl).c_str(),
            [nativeId,currentVM,callback](){
                auto pServer = serverInstances[nativeId].get();
                //不从GC分配，减轻GC压力和内存碎片
                VMObject argObject(ValueType::OBJECT);
                std::vector<VMObject*> paramObjects;
                paramObjects.reserve(pServer->args()); //预留参数空位避免扩容导致指针失效
                auto& aobjmap = argObject.implement.objectImpl;
                //拷贝参数过去
                for(int i = 0;i < pServer->args();i++){
                    //转换成UTF16给引擎回调
                    std::string s = pServer->argName(i).c_str();
                    std::wstring w_argName = string_to_wstring(s);
                    std::string param = pServer->arg(i).c_str();
                    std::wstring w_param = string_to_wstring(param);
                    //paramObjects.emplace_back(ValueType::STRING);
                    //paramObjects.back().implement.stringImpl = w_param;
                    VMObject* vmo = new VMObject(ValueType::STRING); //创建临时的字符串对象，回调返回后需要遍历delete
                    vmo->implement.stringImpl = w_param;
                    paramObjects.push_back(vmo);
                    aobjmap[w_argName] = CreateReferenceVariable(vmo); 
                }
                //在新的执行环境执行回调
                //VMWorker worker(currentVM);
                
                std::vector<VariableValue> argument;
                argument.push_back(CreateReferenceVariable(&argObject));
                //auto result = worker->Init(callback->funcImpl.local_func,&argument); //当前线程运行解释器循环
                auto result = currentVM->InvokeCallback(callback->funcImpl.local_func,argument);
                //发送返回值作为response

                __ESP32_HTTP_SendResponse(result,pServer);

                //清理临时对象
                for(VMObject* paramobj : paramObjects){
                    delete paramobj;
                }   
            });
            return VariableValue();
        });
        objContainer[L"mapPost"] = VM::CreateSystemFunc(2,[](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue{
            if(args[0].getContentType() != ValueType::STRING){
                currentWorker->ThrowError(L"url must be a string");
                return VariableValue();
            }
            if(args[1].getContentType() != ValueType::FUNCTION &&
                args[1].content.function->type != ScriptFunction::Local &&
                args[1].content.function->funcImpl.local_func.arguments.size() != 1){
                currentWorker->ThrowError(L"invaild callback");
                return VariableValue();
            }
            ScriptFunction* callback = args[1].content.function;

            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            auto pServer = serverInstances[nativeId].get();
            VM* currentVM = currentWorker->VMInstance;
            //实现回调层
            pServer->on(wstring_to_string(args[0].content.ref->implement.stringImpl).c_str(),HTTP_POST,
            [nativeId,currentVM,callback](){
                auto pServer = serverInstances[nativeId].get();
                //不从GC分配，减轻GC压力和内存碎片
                VMObject argObject(ValueType::OBJECT);
                std::vector<VMObject*> paramObjects;
                paramObjects.reserve(pServer->args()); //预留参数空位避免扩容导致指针失效
                auto& aobjmap = argObject.implement.objectImpl;
                //拷贝参数过去
                for(int i = 0;i < pServer->args();i++){
                    //转换成UTF16给引擎回调
                    std::string s = pServer->argName(i).c_str();
                    std::wstring w_argName = string_to_wstring(s);
                    std::string param = pServer->arg(i).c_str();
                    std::wstring w_param = string_to_wstring(param);
                    //paramObjects.emplace_back(ValueType::STRING);
                    //paramObjects.back().implement.stringImpl = w_param;
                    VMObject* vmo = new VMObject(ValueType::STRING); //创建临时的字符串对象，回调返回后需要遍历delete
                    vmo->implement.stringImpl = w_param;
                    paramObjects.push_back(vmo);
                    aobjmap[w_argName] = CreateReferenceVariable(vmo); 
                }
                //在新的执行环境执行回调
                //VMWorker worker(currentVM);
                
                std::vector<VariableValue> argument;
                argument.push_back(CreateReferenceVariable(&argObject));
                //auto result = worker->Init(callback->funcImpl.local_func,&argument); //当前线程运行解释器循环
                auto result = currentVM->InvokeCallback(callback->funcImpl.local_func,argument);
                //发送返回值作为response

                __ESP32_HTTP_SendResponse(result,pServer);

                //清理临时对象
                for(VMObject* paramobj : paramObjects){
                    delete paramobj;
                }   
            });
            return VariableValue();
        });

        return CreateReferenceVariable(serverObject);
    });
#endif

#if ESP32_HTTPCLIENT_ENABLED || ESP32_WEBSERVER_ENABLED
    std::wstring httpClassName = L"Http";
    auto httpClassRef = CreateReferenceVariable(httpClass);
    VMInstance->storeGlobalSymbol(httpClassName,httpClassRef);
#endif
}

#if ESP32_SOCKET_ENABLED

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

void ESP32_SocketApi_Init(VM* VMInstance){
    VMObject* socketClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
    socketClass->implement.objectImpl[L"connectTCP"] = VM::CreateSystemFunc(2,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        if(args[0].getContentType() != ValueType::STRING || args[1].getContentType() != ValueType::NUM){
            currentWorker->ThrowError(L"invaild argument type");
            return VariableValue();
        }
        
        std::wstring& wipstr = args[0].content.ref->implement.stringImpl;
        std::string ipstr = wstring_to_string(wipstr);
        uint16_t port = (uint16_t)args[1].content.number;
        
        IPAddress ipAddr;
        if(!ipAddr.fromString(ipstr.c_str())){
            currentWorker->ThrowError(L"invalid IP address format");
            return VariableValue();
        }
        
        static std::unordered_map<uint32_t, std::unique_ptr<WiFiClient>> clientInstances;
        static uint32_t clientInstanceIdSeed = 1;
        
        // 先尝试连接，成功后再创建VM对象
        WiFiClient* nativeClient = new WiFiClient();
        bool connected = nativeClient->connect(ipAddr, port);
        
        if(!connected){
            delete nativeClient; // 连接失败，立即释放内存
            currentWorker->ThrowError(L"connection failed");
            return VariableValue();
        }
        
        // 连接成功，创建VM对象
        uint32_t clientId = clientInstanceIdSeed++;
        clientInstances[clientId] = std::unique_ptr<WiFiClient>(nativeClient);
        
        VMObject* socketObject = currentWorker->VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
        auto& objContainer = socketObject->implement.objectImpl;
        
        objContainer[L"_id"] = CreateNumberVariable((double)clientId);
        objContainer[L"ip"] = args[0];
        objContainer[L"port"] = CreateNumberVariable(args[1].content.number);
        objContainer[L"connected"] = CreateBooleanVariable(true);
        
        // 创建析构函数
        objContainer[L"finalize"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            
            if(clientInstances.find(nativeId) != clientInstances.end()){
                // 直接释放资源，不需要检查连接状态
                clientInstances.erase(nativeId);
            }
            
            return CreateBooleanVariable(true);
        });
        
        // 发送数据 (buf,offset,length)
        objContainer[L"send"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            if(args[2].getContentType() != ValueType::NUM || args[1].getContentType() != ValueType::NUM){
                currentWorker->ThrowError(L"invaild argument type");
                return VariableValue();
            }
            uint32_t offset = (uint32_t)args[1].content.number;
            uint32_t length = (uint32_t)args[2].content.number;
            if(args[0].getContentType() == ValueType::OBJECT){
                auto& obj = args[0].content.ref->implement.objectImpl;
                if(obj.find(L"bufid") != obj.end()){
                    uint32_t bufid = obj[L"bufid"].content.number;
                    auto bufinfo = GetByteBufferInfo(bufid);
                    if(!bufinfo.data){
                        currentWorker->ThrowError(L"invaild Buffer");
                        return VariableValue();
                    }
                    uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
                    if(clientInstances.find(nativeId) == clientInstances.end()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    WiFiClient* client = clientInstances[nativeId].get();
                    if(!client->connected()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    if (offset >= bufinfo.length || offset + length > bufinfo.length || length <= 0) {
                        currentWorker->ThrowError(L"out of range");
                        return VariableValue();
                    }
                    client->write_P((const char*)(bufinfo.data + offset),length);
                    return VariableValue();
                }
            }
            currentWorker->ThrowError(L"send only apply Buffer");
            return VariableValue();
        });
        
        // recv(buf,offset,buf_length) : number 返回值表示收到的大小
        objContainer[L"recv"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            if(args[2].getContentType() != ValueType::NUM || args[1].getContentType() != ValueType::NUM){
                currentWorker->ThrowError(L"invaild argument type");
                return VariableValue();
            }
            uint32_t offset = (uint32_t)args[1].content.number;
            uint32_t length = (uint32_t)args[2].content.number;
            if(args[0].getContentType() == ValueType::OBJECT){
                auto& obj = args[0].content.ref->implement.objectImpl;
                if(obj.find(L"bufid") != obj.end()){
                    uint32_t bufid = obj[L"bufid"].content.number;
                    auto bufinfo = GetByteBufferInfo(bufid);
                    if(!bufinfo.data){
                        currentWorker->ThrowError(L"invaild Buffer");
                        return VariableValue();
                    }
                    uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
                    if(clientInstances.find(nativeId) == clientInstances.end()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    WiFiClient* client = clientInstances[nativeId].get();
                    if(!client->connected()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    if (offset >= bufinfo.length || offset + length > bufinfo.length || length <= 0) {
                        currentWorker->ThrowError(L"out of range");
                        return VariableValue();
                    }
                    uint32_t ret = client->readBytes(bufinfo.data + offset,length);
                    return CreateNumberVariable(ret);
                }
            }
            currentWorker->ThrowError(L"send only apply Buffer");
            return VariableValue();
        });
        //recvSize(buf,offset,buf_length) : boolean
        objContainer[L"recvSize"] = VM::CreateSystemFunc(3, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            if(args[2].getContentType() != ValueType::NUM || args[1].getContentType() != ValueType::NUM){
                currentWorker->ThrowError(L"invaild argument type");
                return VariableValue();
            }
            uint32_t offset = (uint32_t)args[1].content.number;
            uint32_t length = (uint32_t)args[2].content.number;
            if(args[0].getContentType() == ValueType::OBJECT){
                auto& obj = args[0].content.ref->implement.objectImpl;
                if(obj.find(L"bufid") != obj.end()){
                    uint32_t bufid = obj[L"bufid"].content.number;
                    auto bufinfo = GetByteBufferInfo(bufid);
                    if(!bufinfo.data){
                        currentWorker->ThrowError(L"invaild Buffer");
                        return VariableValue();
                    }
                    uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
                    if(clientInstances.find(nativeId) == clientInstances.end()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    WiFiClient* client = clientInstances[nativeId].get();
                    if(!client->connected()){
                        currentWorker->ThrowError(L"connection has closed");
                        return VariableValue();
                    }
                    if (offset >= bufinfo.length || offset + length > bufinfo.length || length <= 0) {
                        currentWorker->ThrowError(L"out of range");
                        return VariableValue();
                    }
                    bool ret = receiveExactSize(*client,bufinfo.data,bufinfo.length);
                    return CreateBooleanVariable(ret);
                }
            }
            currentWorker->ThrowError(L"send only apply Buffer");
            return VariableValue();
        });
        
        // 检查连接状态
        objContainer[L"isConnected"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            
            if(clientInstances.find(nativeId) == clientInstances.end()){
                return CreateBooleanVariable(false);
            }
            
            WiFiClient* client = clientInstances[nativeId].get();
            bool isConnected = client->connected();
            
            if(!isConnected){
                clientInstances.erase(nativeId);
            }
            
            return CreateBooleanVariable(isConnected);
        });
        
        // 断开连接
        objContainer[L"close"] = VM::CreateSystemFunc(0, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            
            if(clientInstances.find(nativeId) != clientInstances.end()){
                clientInstances.erase(nativeId);
            }
            
            
            return CreateBooleanVariable(true);
        });
        
        // 设置超时
        objContainer[L"setTimeout"] = VM::CreateSystemFunc(1, [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
            if(args[0].getContentType() != ValueType::NUM){
                currentWorker->ThrowError(L"timeout must be a number");
                return VariableValue();
            }
            
            uint32_t nativeId = (uint32_t)thisValue->implement.objectImpl[L"_id"].content.number;
            
            if(clientInstances.find(nativeId) != clientInstances.end()){
                clientInstances[nativeId]->setTimeout((int)args[0].content.number);
                return CreateBooleanVariable(true);
            }
            
            return CreateBooleanVariable(false);
        });
        
        return CreateReferenceVariable(socketObject);
    });

    std::wstring socketClassName = L"Socket";
    auto socketClassRef = CreateReferenceVariable(socketClass);
    VMInstance->storeGlobalSymbol(socketClassName,socketClassRef);
}

#endif

void ESP32_NetworkApi_Init(VM* VMInstance){
#if ESP32_WEBSERVER_ENABLED
    ESP32_HTTPApi_Init(VMInstance);
#endif
#if ESP32_SOCKET_ENABLED
    ESP32_SocketApi_Init(VMInstance);
#endif
}

void ESP32_WifiHardwareApi_Init(VM* VMInstance){
    VMObject* wifiClass = VMInstance->currentGC->GC_NewObject(ValueType::OBJECT);
    wifiClass->implement.objectImpl[L"softAP"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        //WiFi.softAP(ssid);
        if(args.size() == 1 && args[0].getContentType() == ValueType::STRING){
            std::string ssid = wstring_to_string(args[0].content.ref->implement.stringImpl);
            bool result = WiFi.softAP(ssid.c_str());
            return CreateBooleanVariable(result);
        } //WiFi.softAP(ssid,pass);
        else if(args.size() == 2 
        && args[0].getContentType() == ValueType::STRING
        && args[1].getContentType() == ValueType::STRING){
            std::string ssid = wstring_to_string(args[0].content.ref->implement.stringImpl);
            std::string password = wstring_to_string(args[1].content.ref->implement.stringImpl);
            bool result = WiFi.softAP(ssid.c_str(),password.c_str());
            return CreateBooleanVariable(result);
        }
        else{
            currentWorker->ThrowError(L"softAP() invaild argument");
            return VariableValue();
        }

    });
    wifiClass->implement.objectImpl[L"connect"] = VM::CreateSystemFunc(DYNAMIC_ARGUMENT,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        //WiFi.connect(ssid);
        if(args.size() == 1 && args[0].getContentType() == ValueType::STRING){
            std::string ssid = wstring_to_string(args[0].content.ref->implement.stringImpl);
            bool result = WiFi.begin(ssid.c_str());
            return CreateBooleanVariable(result);
        } //WiFi.connect(ssid,pass);
        else if(args.size() == 2 
        && args[0].getContentType() == ValueType::STRING
        && args[1].getContentType() == ValueType::STRING){
            std::string ssid = wstring_to_string(args[0].content.ref->implement.stringImpl);
            std::string password = wstring_to_string(args[1].content.ref->implement.stringImpl);
            bool result = WiFi.begin(ssid.c_str(),password.c_str());
            return CreateBooleanVariable(result);
        }
        else{
            currentWorker->ThrowError(L"WiFi.connect() invaild argument");
            return VariableValue();
        }
        
    });
    wifiClass->implement.objectImpl[L"isConnected"] = VM::CreateSystemFunc(0,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        
        return CreateBooleanVariable(WiFi.status() == WL_CONNECTED);
    });
    wifiClass->implement.objectImpl[L"setMode"] = VM::CreateSystemFunc(1,[](std::vector<VariableValue>& args, VMObject* thisValue,VMWorker* currentWorker) -> VariableValue{
        if(args[0].getContentType() != ValueType::STRING){
            currentWorker->ThrowError(L"setMode invaild argument");
        }
        auto& mode = args[0].content.ref->implement.stringImpl;
        if(mode == L"ap"){
            WiFi.mode(WIFI_MODE_AP);
        }else if(mode == L"sta"){
            WiFi.mode(WIFI_MODE_STA);
        }else if(mode == L"ap_sta"){
            WiFi.mode(WIFI_MODE_APSTA);
        }
        else if(mode == L"none"){
            WiFi.mode(WIFI_MODE_NULL);
        }
        else{
            currentWorker->ThrowError(L"setMode unknown command " + mode);
        }
        
        return VariableValue();
    });

    // WiFi.localIP
    wifiClass->implement.objectImpl[L"localIP"] = VM::CreateSystemFunc(0,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        IPAddress ip = WiFi.localIP();
        String ipStr = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);
        VMObject* strObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(
            string_to_wstring(ipStr.c_str()));
        return CreateReferenceVariable(strObj);
    });

    // WiFi.SSID
    wifiClass->implement.objectImpl[L"SSID"] = VM::CreateSystemFunc(0,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        String ssid = WiFi.SSID();
        VMObject* strObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(
            string_to_wstring(ssid.c_str()));
        return CreateReferenceVariable(strObj);
    });

    // WiFi.RSSI
    wifiClass->implement.objectImpl[L"RSSI"] = VM::CreateSystemFunc(0,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        int32_t rssi = WiFi.RSSI();
        return CreateNumberVariable(rssi);
    });

    // WiFi.macAddress
    wifiClass->implement.objectImpl[L"macAddress"] = VM::CreateSystemFunc(0,
    [](std::vector<VariableValue>& args, VMObject* thisValue, VMWorker* currentWorker) -> VariableValue {
        String mac = WiFi.macAddress();
        VMObject* strObj = currentWorker->VMInstance->currentGC->GC_NewStringObject(
            string_to_wstring(mac.c_str()));
        return CreateReferenceVariable(strObj);
    });

    std::wstring className = L"WiFi";
    auto ref = CreateReferenceVariable(wifiClass);
    VMInstance->storeGlobalSymbol(className,ref);
}

void ESP32_WiFiPlatformApi_Init(VM* VMInstance){
   ESP32_WifiHardwareApi_Init(VMInstance);
   ESP32_NetworkApi_Init(VMInstance);
}



#endif

#endif