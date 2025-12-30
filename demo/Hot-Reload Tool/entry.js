function BeginRecvClient(SSID, Password, targetUrl) {
    WiFi.connect(SSID, Password);
    println("start connecting wifi...");
    while (!WiFi.isConnected()) {
        println("connecting....");
        System.delay(500);
    }

    while (true) {
        try {
            let resp = Http.fetch(targetUrl);
            println("res:" + resp);
            if (resp.ok) {
                let buf = resp.buffer();
                let file = FS.open("/temp.nejs", FS.FILE_WRITE);
                file.write(0, buf.size, buf);
                file.close();
                buf.close();
                Gpio.set(2, true); //打开led，表明程序加载
                println("result:" + require("/temp.nejs"));  //GC在模块彻底无引用后会自动卸载
                Gpio.set(2, false);
                System.gc();
            } else {
                println("response illegal");
            }
        } catch (ex) {
            println("download program failed\n" + ex);
        }
        println("the process end, GPIO25--3v3 to restart.");
        while (!Gpio.read(25)) {
            System.delay(10);
        }
        println("restarting..");
    }
}

function getConfigPageHtml() {
    let page = "";
    page += "<!DOCTYPE html>";
    page += "<html lang='zh-CN'>";
    page += "<head>";
    page += "<meta charset='UTF-8'>";
    page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    page += "<title>设备配置</title>";
    page += "<style>";
    page += "body { font-family: Arial, sans-serif; max-width: 400px; margin: 50px auto; padding: 20px; }";
    page += "h2 { text-align: center; color: #333; }";
    page += ".form-group { margin-bottom: 20px; }";
    page += "label { display: block; margin-bottom: 5px; color: #555; }";
    page += "input { width: 100%; padding: 10px; border: 1px solid #ddd; border-radius: 4px; box-sizing: border-box; }";
    page += "button { width: 100%; padding: 12px; background: #007bff; color: white; border: none; border-radius: 4px; cursor: pointer; font-size: 16px; }";
    page += "button:hover { background: #0056b3; }";
    page += "</style>";
    page += "</head>";
    page += "<body>";
    page += "<h2>设备配置</h2>";
    page += "<form method='POST' action='/config'>";
    page += "<div class='form-group'>";
    page += "<label for='ssid'>WiFi SSID</label>";
    page += "<input type='text' id='ssid' name='SSID' required placeholder='输入WiFi名称'>";
    page += "</div>";
    page += "<div class='form-group'>";
    page += "<label for='password'>WiFi密码</label>";
    page += "<input type='password' id='password' name='Password' required placeholder='输入WiFi密码'>";
    page += "</div>";
    page += "<div class='form-group'>";
    page += "<label for='url'>服务器URL</label>";
    page += "<input type='url' id='url' name='Url' required placeholder='https://example.com/your_program.nejs' value='http://192.168.4.1/config'>";
    page += "</div>";
    page += "<button type='submit'>保存配置</button>";
    page += "</form>";
    page += "</body>";
    page += "</html>";
    System.gc(); //拼接太多字符串了，必须调用GC删一下
    return page;
}

function StartAPConfig() {
    WiFi.softAP("NexusEJS-SmartConfig");
    println("CurrentIP:" + WiFi.localIP());
    let server = Http.createServer(80);
    let complete = {value:false};
    server.mapGet("/", ctx => {
        return {
            code: 200,
            type: "text/html",
            content: getConfigPageHtml()
        };
    });
    server.mapPost("/config", ctx => {
        let file = FS.open("/config.txt",FS.FILE_WRITE);
        let buf = Buffer.create(1024);
        buf.writeUTF8(0,ctx.SSID + "|" + ctx.Password + "|" + ctx.Url);
        file.write(0,buf.size,buf);
        file.close();
        buf.close();
        complete.value = true;
        return "ok";
    });
    server.begin();
    while (!complete.value) {
        System.delay(1000);
    }
    server = null;
    System.gc();
    System.reboot();
}


//检测是否存在
if (FS.exists("/config.txt")) {
    let cfg = FS.open("/config.txt", FS.FILE_READ);
    let buf = Buffer.create(cfg.size());
    cfg.read(buf, 0, cfg.size());
    cfg.close();
    //<ssid>,<password>,<url>
    let options = buf.readUTF8(0).split("|");
    BeginRecvClient(options[0], options[1], options[2]);
}
else {
    StartAPConfig();
}
