const ST7735 = {
    create(DC) {
        const spi = SPI.init(1, 5, 80000000, SPI.MODE_0, SPI.MSBFIRST, 16);

        function cmd(payload) {
            Gpio.set(DC, false);
            spi.write(payload);
        }

        function data(payload) {
            Gpio.set(DC, true);
            spi.write(payload);
        }

        // 1. 硬件复位（GPIO4）
        Gpio.set(4, true);
        vtDelay(100);
        Gpio.set(4, false);
        vtDelay(100);
        Gpio.set(4, true);
        vtDelay(100);

        // 2. 软件复位
        cmd(0x01);  // SWRESET
        vtDelay(150);

        // 3. 退出睡眠模式
        cmd(0x11);  // SLPOUT
        vtDelay(500);

        // 4. 帧率控制
        cmd(0xB1);  // FRMCTR1
        data(0x01);
        data(0x2C);
        data(0x2D);

        cmd(0xB2);  // FRMCTR2
        data(0x01);
        data(0x2C);
        data(0x2D);

        cmd(0xB3);  // FRMCTR3
        data(0x01);
        data(0x2C);
        data(0x2D);
        data(0x01);
        data(0x2C);
        data(0x2D);

        // 5. 显示反转控制
        cmd(0xB4);  // INVCTR
        data(0x07);

        // 7. 内存数据访问控制
        cmd(0x36);  // MADCTL
        data(0xC8);  // RGB顺序

        // 8. 像素格式
        cmd(0x3A);  // COLMOD
        data(0x05);  // 16-bit RGB565

        //设置列地址
        cmd(0x2A);  // CASET
        data(0x00);
        data(0x00);
        data(0x00);
        data(0x7F);  // 128列

        // 11. 设置行地址
        cmd(0x2B);  // RASET
        data(0x00);
        data(0x00);
        data(0x00);
        data(0x9F);  // 160行

        // 12. 打开显示
        cmd(0x29);  // DISPON
        vtDelay(100);

        // 13. 清除显示（黑色）

        let bufSize = 128 * 160 * 2;
        let buf = Buffer.create(bufSize);
        buf.fill(0, 0, bufSize);

        cmd(0x2C);  // RAMWR

        data(buf);

        buf.close();
        buf = null;
        System.gc();

        const fontWidth = 16;
        const fontHeight = 16;
        const fontSize = fontWidth * fontHeight / 8;

        let fontData = null;
        const fontPath = "/font12x16.bin";
        if (FS.exists(fontPath)) {
            fontData = FS.open(fontPath, FS.FILE_READ);
            if (!fontData) {
                println("无法打开字体文件，字体将为null！");
            }
            else {
                buf = Buffer.create(2);
                if (fontData.read(buf, 0, 2) === 2) {
                    fontWidth = buf.readUInt(0, 1);
                    fontHeight = buf.readUInt(1, 1);
                    fontSize = fontWidth * fontHeight / 8;
                }
            }
        }

        function getCharDataOffset(code) {
            const headerSize = 2;
            if (code >= 0x20 && code <= 0x7F) { //ASCII区
                return headerSize + (code - 0x20) * fontSize;
            } else { //其他都认为是中文区的
                return headerSize + 96 * fontSize + (code - 0x4E00) * fontSize;
            }
        }

        return {
            screenWidth: 128,
            screenHeight: 160,

            close() {
                spi.close();
            },
            setWindow(x0, y0, x1, y1) {
                // 设置列地址范围
                cmd(0x2A);  // CASET
                data((x0 >> 8) & 0xFF);
                data(x0 & 0xFF);
                data((x1 >> 8) & 0xFF);
                data(x1 & 0xFF);

                // 设置行地址范围
                cmd(0x2B);  // RASET
                data((y0 >> 8) & 0xFF);
                data(y0 & 0xFF);
                data((y1 >> 8) & 0xFF);
                data(y1 & 0xFF);
            },
            commitWindow(buf) {
                cmd(0x2C);
                data(buf);
            },
            clear(black) {
                //this.setWindow(0, 0, 128, 160);
                println("clear");
                vtDelay(100);

                let color = 0xFF;

                if (black) color = 0x00;

                let bufSize = 32 * 32 * 2;
                let buf = Buffer.create(bufSize);

                buf.fill(color, 0, bufSize);

                for (let i = 0; i < this.screenHeight; i += 32) {
                    for (let j = 0; j < this.screenWidth; j += 32) {
                        this.setWindow(j, i, j + 31, i + 31);
                        this.commitWindow(buf);
                    }
                }

                buf.close();
            },
            //省内存但是逐字绘制
            drawString(x, y, str, color) {

                const fontBuffer = Buffer.create(fontSize); //从文件系统读取字体用的缓冲
                const screenBuffer = Buffer.create(fontWidth * fontHeight * 2); //屏幕缓冲

                let currentX = x;
                let currentY = y;
                for (let charOffset = 0; charOffset < str.length; charOffset++) {
                    screenBuffer.fill(0, 0, screenBuffer.size);
                    let charCode = str.charCodeAt(charOffset);
                    if (charCode === 10) {
                        currentY += fontHeight;
                        currentX = x;
                        continue;
                    }
                    let offset = getCharDataOffset(charCode);
                    fontData.seek(offset, 0);
                    fontData.read(fontBuffer, 0, fontSize);
                    this.setWindow(currentX, currentY, currentX + fontWidth - 1, currentY + fontHeight - 1);

                    //从PageMajor位图转到屏幕，有点麻烦
                    for (let i = 0; i < fontHeight / 8; i++) {
                        for (let j = 0; j < fontWidth; j++) {
                            let page = fontBuffer.readUInt(i * fontWidth + j, 1);
                            for (let bit = 0; bit < 8; bit++) {

                                if (page & (1 << bit)) {
                                    let vx = j;
                                    let vy = i * 8 + bit;
                                    screenBuffer.writeUInt((vy * fontWidth + vx) * 2, 2, color);
                                }
                            }
                        }
                    }
                    this.commitWindow(screenBuffer);

                    currentX += fontWidth;
                    if (currentX + fontWidth > this.screenWidth) {
                        currentX = x;
                        currentY += fontHeight;
                    }
                    if (currentY + fontHeight > this.screenHeight) {
                        break; //放弃超出去的部分
                    }
                }

                fontBuffer.close();
                screenBuffer.close();

            },
            drawStringLines(x, y, str, color) {
                // 计算最大字符数（避免超出屏幕）
                const maxChars = Math.floor((this.screenWidth - x) / fontWidth);
                const drawStr = str.substring(0, maxChars);
                const charCount = drawStr.length;

                if (charCount === 0 || y + fontHeight > this.screenHeight) {
                    return; // 没有可绘制的字符或超出屏幕底部
                }

                // 创建行缓冲区（整个字符行的像素数据）
                const lineBuffer = Buffer.create(fontWidth * charCount * fontHeight * 2);
                lineBuffer.fill(0, 0, lineBuffer.size);
                const fontBuffer = Buffer.create(fontSize);

                // 遍历每个字符
                for (let i = 0; i < charCount; i++) {
                    const charCode = drawStr.charCodeAt(i);
                    const offset = getCharDataOffset(charCode);

                    // 读取字体数据
                    fontData.seek(offset, 0);
                    fontData.read(fontBuffer, 0, fontSize);

                    // 将字体数据绘制到行缓冲区的对应位置
                    const charXOffset = i * fontWidth; // 字符在行中的X偏移（像素）

                    // 遍历字体的每个像素
                    for (let py = 0; py < fontHeight; py++) {
                        for (let px = 0; px < fontWidth; px++) {
                            // 计算字体数据中的位索引
                            const byteIndex = Math.floor(py / 8) * fontWidth + px;
                            const bitIndex = py % 8;

                            if (fontBuffer.readUInt(byteIndex, 1) & (1 << bitIndex)) {
                                // 计算在行缓冲区中的像素位置
                                const bufferX = charXOffset + px;
                                const bufferY = py;
                                const pixelIndex = (bufferY * fontWidth * charCount + bufferX) * 2;

                                // 写入颜色（RGB565格式）
                                lineBuffer.writeUInt(pixelIndex, 2, color);
                            }
                        }
                    }
                }

                // 设置窗口（整行区域）
                this.setWindow(x, y, x + fontWidth * charCount - 1, y + fontHeight - 1);

                // 提交整个行缓冲区
                this.commitWindow(lineBuffer);

                // 释放缓冲区
                lineBuffer.close();
                fontBuffer.close();
            },
            fillRect(x1, y1, x2, y2, color) {
                this.setWindow(x1, y1, x2, y2);
                let buf = Buffer.create((x2 - x1 + 1) * (y2 - y1 + 1));
                for (let i = 0; i < buf.size; i += 2) {
                    buf.writeUInt(i, 2, color);
                }
                this.commitWindow(buf);
                buf.close();
            },
            drawRect(x1, y1, x2, y2, color, lineWidth = 1) {
                // 上边
                this.fillRect(x1, y1, x2, y1 + lineWidth - 1, color);
                // 下边
                this.fillRect(x1, y2 - lineWidth + 1, x2, y2, color);
                // 左边
                this.fillRect(x1, y1 + lineWidth, x1 + lineWidth - 1, y2 - lineWidth, color);
                // 右边
                this.fillRect(x2 - lineWidth + 1, y1 + lineWidth, x2, y2 - lineWidth, color);
            }
        }

    }
}

return ST7735;