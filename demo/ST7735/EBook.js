const ST7735 = require("ST7735.nejs");

const st = ST7735.create(2)

let bit5Size = 0x1F / 0xFF;
let bit6Size = 0x3F / 0xFF;

function rgb(r, g, b) {
    let color = (b * bit5Size) << 11;
    color |= (g * bit6Size) << 5;
    color |= (r * bit5Size);
    let swap = (color >> 8) | ((color & 0xFF) << 8);
    return swap;
}

const EBookDirPath = "/ebooks";
const NextButtionPin = 33;
const SubmitButtonPin = 32;
const BackLightPin = 22;

//12x16 font
function choice(items, title) {
    st.fillRect(0, 18, 128, 19, rgb(255, 255, 255));
    st.drawStringLines(10, 1, title, rgb(255, 255, 14))
    const itemStartY = 24;
    const itemStartX = 10;
    const itemHeight = 16;

    let activedColor = rgb(0, 213, 255);
    let normalColor = rgb(255, 255, 255);

    let selected = 0;
    for (let i = 0; i < items.length; i++) {
        if (i == selected) {
            st.drawString(itemStartX, itemStartY + itemHeight * i, items[i], activedColor);
            continue;
        }
        st.drawString(itemStartX, itemStartY + itemHeight * i, items[i], normalColor);
    }

    while (true) {
        if (Gpio.read(NextButtionPin)) {
            st.drawStringLines(itemStartX, itemStartY + itemHeight * selected, items[selected], normalColor);
            selected = (selected + 1) % items.length;
            st.drawString(itemStartX, itemStartY + itemHeight * selected, items[selected], activedColor);
        }
        if (Gpio.read(SubmitButtonPin)) {
            st.clear(true);
            return selected;
        }

        vtDelay(10); //按钮消抖并让出CPU
    }
}

function displayEBook(path) {
    let name = path.split("/");
    name = name[name.length - 1];
    const PageCharCount = 100;
    const PageBytesCount = 256;
    let file = FS.open(path, FS.FILE_READ);
    let buf = Buffer.create(4);
    let contentBuf = Buffer.create(PageBytesCount);
    let bytePosition = 0;  //当前页在文件中的起始位置
    let page = 0;

    function refreshContent() {
        contentBuf.fill(0, 0, contentBuf.size);

        let charCount = 0;      //已读取字符数
        let contentOffset = 0;  //contentBuf的写入偏移
        let currentFilePos = bytePosition; 

        let currentLineCharCount = 0;
        let lineIndex = 0;

        while (charCount < PageCharCount) {
            //定位到当前要读取的位置
            file.seek(currentFilePos, 0);

            //读取1字节判断UTF-8长度
            let ret = file.read(buf, 0, 1);
            if (ret <= 0) break;  //文件结束

            let byte = buf.readUInt(0, 1);
            if (byte === 0) break;

            if (byte === 10) { //换行符
                currentLineCharCount = 0;
                lineIndex += 1;
            }

            if (currentLineCharCount >= 10) {
                currentLineCharCount = 0;
                lineIndex += 1;
            }

            if (lineIndex >= 8) {
                break;
            }

            currentLineCharCount++;

            //println("line=" + lineIndex + "  linechar=" + currentLineCharCount);

            let charBytes = 1;
            if ((byte & 0x80) === 0) {
                charBytes = 1;
            } else if ((byte & 0xE0) === 0xC0) {
                charBytes = 2;
            } else if ((byte & 0xF0) === 0xE0) {
                charBytes = 3;
            } else if ((byte & 0xF8) === 0xF0) {
                charBytes = 4;
            } else {
                charBytes = 1;  // 非法字节，当作ASCII
                println("非法UTF-8字节：" + byte);
            }

            // 检查缓冲区空间
            if (contentOffset + charBytes > PageBytesCount) {
                break;
            }

            // 关键修复：重新定位，读取完整字符
            file.seek(currentFilePos, 0);
            let readRet = file.read(contentBuf, contentOffset, charBytes);

            if (readRet < charBytes) {
                break;  // 文件不够读了
            }

            // 推进各个指针
            contentOffset += charBytes;   // 缓冲区写入位置推进
            currentFilePos += charBytes;  // 文件读取位置推进（关键！）
            charCount++;
        }

        // 更新全局文件位置到下一页
        bytePosition = currentFilePos;

        // 显示
        st.clear(true);
        st.fillRect(0, 18, 128, 19, rgb(255, 255, 255));
        st.drawStringLines(10, 1, name, rgb(255, 255, 14));
        st.drawString(0, 20, contentBuf.readUTF8(0, contentOffset), rgb(255, 255, 255));
    }

    refreshContent();

    while (true) {
        if (Gpio.read(NextButtionPin)) {
            page++;
            refreshContent();
            while (Gpio.read(NextButtionPin)) { delay(10); }
        }

        if (Gpio.read(SubmitButtonPin)) {
            st.clear(true);
            buf.close();
            contentBuf.close();
            file.close();
            return;
        }
        delay(10);
    }
}

function EBookMenu(path) {
    let items = ["阅读", "删除", "返回"];
    let select = choice(items, "选择要进行的操作");
    if (select === 0) {
        displayEBook(path);
    } else if (select === 1) {
        if (FS.remove(path)) {
            st.drawString(30, 50, "删除成功", rgb(0, 255, 55));
        } else {
            st.drawString(30, 50, "删除失败", rgb(255, 0, 0));
        }
        vtDelay(1000);
        st.clear(true);
    }
}

function getIndexHtml() {
    return "<html><form action=/upload method=post accept-charset=utf-8>Title:<input name=title><br>Content:<textarea name=content rows=6 cols=30></textarea><br><input type=submit></form></html>";
}


Gpio.set(BackLightPin, true);

Gpio.set(12, true);

while (true) {
    let mainMenu = ["打开热点", "电子书", "屏幕休眠",  "退出系统"];
    let main_choice = choice(mainMenu, "牢林OS");
    if (main_choice === 0) {
        WiFi.setMode("ap_sta");
        WiFi.softAP("NexusEJS");
        st.drawString(40, 70, "热点模式", rgb(85, 215, 255));
        st.drawString(10, 90, WiFi.localIP(), rgb(255, 255, 255));
        const server = Http.createServer(80);
        server.mapPost("/upload", ctx => {
            let file = null;
            if (FS.exists(EBookDirPath + "/" + ctx.title)) {
                println("追加模式");
                file = FS.open(EBookDirPath + "/" + ctx.title, FS.FILE_APPEND);
                file.write(0, 0, ctx.content);
                file.close();
                return "append success";
            } else {
                file = FS.open(EBookDirPath + "/" + ctx.title, FS.FILE_WRITE);
                file.write(0, 0, ctx.content);
                file.close();
                return "create success";
            }
        });
        server.mapGet("/", ctx => {
            return {
                type: "text/html",
                content: getIndexHtml(),
                code: 200
            }
        });
        server.begin();
        while (!Gpio.read(SubmitButtonPin)) {
            vtDelay(100);
        }

        server.close();
        WiFi.setMode("sta");
        st.clear(true);

    } else if (main_choice === 1) {
        let files = FS.listDir(EBookDirPath).map(file => file.name);
        files.push("返回");
        println(files);
        let bookIndex = choice(files, "请选择电子书");
        if (bookIndex != files.length - 1) {
            EBookMenu(EBookDirPath + "/" + files[bookIndex]);
        }
    } else if (main_choice === 2) {
        st.drawString(10, 50, "通过确认按钮唤醒屏幕", rgb(85, 215, 255));
        vtDelay(1000);
        Gpio.set(BackLightPin, false);
        st.clear(true);
        while (!Gpio.read(SubmitButtonPin)) {
            vtDelay(100);
        }
        Gpio.set(BackLightPin, true);
    }
    else if (main_choice === 3) {
        st.drawString(20, 50, "正在退出...", rgb(85, 215, 255));
        vtDelay(2000);
        st.clear(true);
        break;
    }
}

Gpio.set(BackLightPin, false);
st.close();
