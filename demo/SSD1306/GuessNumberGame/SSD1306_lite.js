//NexusEJS SSD1306驱动
//github.com/xy660/NexusEJS

const SSD1306 = {

    create(cs, dc, rst) {
        // 引脚定义
        const CS = cs;
        const DC = dc;
        const RST = rst;

        // 屏幕参数
        const WIDTH = 128;
        const HEIGHT = 64;

        // 显存
        const buffer = Buffer.create(1024);

        // 初始化Buffer为全0
        let i = 0;
        while (i < 1024) {
            buffer.writeUInt(i, 1, 0x00);
            i = i + 1;
        }

        // 复位屏幕
        Gpio.set(RST, false);
        delay(10);
        Gpio.set(RST, true);
        delay(10);

        // 初始化SPI
        const spi = SPI.init(2, CS, 400000, SPI.MODE_0, SPI.MSBFIRST, 8);

        // 发送命令
        function cmd(c) {
            Gpio.set(DC, false);
            spi.write(c);
        }

        // 发送数据
        function data(d) {
            Gpio.set(DC, true);
            spi.write(d);
        }

        // 发送Buffer
        function sendBuffer(buf) {
            Gpio.set(DC, true);
            spi.write(buf);
        }

        // 初始化屏幕
        println("初始化SSD1306...");

        cmd(0xAE);  // 关显示
        cmd(0xD5);
        cmd(0x80);
        cmd(0xA8);
        cmd(0x3F);
        cmd(0xD3);
        cmd(0x00);
        cmd(0x40);
        cmd(0x8D);
        cmd(0x14);
        cmd(0x20);
        cmd(0x00);
        cmd(0xA1);
        cmd(0xC8);
        cmd(0xDA);
        cmd(0x12);
        cmd(0x81);
        cmd(0xFF);
        cmd(0xD9);
        cmd(0xF1);
        cmd(0xDB);
        cmd(0x40);
        cmd(0xA4);
        cmd(0xA6);
        cmd(0xAF);  // 开显示

        delay(100);
        println("SSD1306初始化完成");

        // 清屏
        cmd(0x21);
        cmd(0x00);
        cmd(0x7F);
        cmd(0x22);
        cmd(0x00);
        cmd(0x07);

        i = 0;
        while (i < 1024) {
            data(0x00);
            i = i + 1;
        }

        delay(100);

        // 返回接口
        return {
            // 设置像素点
            setPixel(x, y, light) {
                if (x < 0 || x >= WIDTH || y < 0 || y >= HEIGHT) {
                    return false;
                }

                const page = Math.floor(y / 8);
                const bit = y % 8;
                const index = x + page * 128;

                let current = buffer.readUInt(index, 1);

                if (light) {
                    current = current | (1 << bit);
                } else {
                    current = current & ~(1 << bit);
                }

                buffer.writeUInt(index, 1, current);
            },

            getInnerBuffer() {
                return buffer;
            },

            setInverseColor(inverse) {
                if (inverse) {
                    cmd(0xA7);
                } else {
                    cmd(0xA6);
                }
            },

            // 提交到屏幕
            commit() {
                cmd(0x21);
                cmd(0x00);
                cmd(0x7F);
                cmd(0x22);
                cmd(0x00);
                cmd(0x07);

                sendBuffer(buffer);
            },

            // 清屏
            clear() {
                let i = 0;
                while (i < 1024) {
                    buffer.writeUInt(i, 8, 0x00);
                    i += 8;
                }
            },

            drawLine(x1, y1, x2, y2) {
                // 计算线段长度
                const dx = Math.abs(x2 - x1);
                const dy = Math.abs(y2 - y1);

                // 决定步数（取较长边）
                const steps = Math.max(dx, Math.max(dy, 1));

                // 计算每步增量
                const xIncrement = (x2 - x1) / steps;
                const yIncrement = (y2 - y1) / steps;

                // 绘制每个点
                let x = x1;
                let y = y1;

                for (let i = 0; i <= steps; i++) {
                    this.setPixel(Math.round(x), Math.round(y), true);
                    x += xIncrement;
                    y += yIncrement;
                }
            },

            // 画矩形
            drawRect(x, y, width, height) {
                this.drawLine(x, y, x + width, y);
                this.drawLine(x + width, y, x + width, y + height);
                this.drawLine(x, y, x, y + height);
                this.drawLine(x, y + height, x + width, y + height);
            },

            // 填充矩形
            fillRect(x, y, width, height, light) {
                if (x >= WIDTH || y >= HEIGHT) return false;
                if (width <= 0 || height <= 0) return false;

                const right = Math.min(x + width, WIDTH);
                const bottom = Math.min(y + height, HEIGHT);
                const w = right - x;
                const h = bottom - y;

                // 页对齐优化
                if (y % 8 === 0 && h % 8 === 0) {
                    const startPage = Math.floor(y / 8);
                    const pageCount = h / 8;

                    for (let p = 0; p < pageCount; p++) {
                        let addr = (startPage + p) * 128 + x;
                        let remaining = w;

                        if (light) {
                            // 填充0xFF
                            while (remaining >= 4) {
                                buffer.writeUInt(addr, 4, 0xFFFFFFFF);
                                addr += 4;
                                remaining -= 4;
                            }
                            while (remaining > 0) {
                                buffer.writeUInt(addr, 1, 0xFF);
                                addr += 1;
                                remaining -= 1;
                            }
                        } else {
                            // 填充0x00
                            while (remaining >= 4) {
                                buffer.writeUInt(addr, 4, 0x00000000);
                                addr += 4;
                                remaining -= 4;
                            }
                            while (remaining > 0) {
                                buffer.writeUInt(addr, 1, 0x00);
                                addr += 1;
                                remaining -= 1;
                            }
                        }
                    }
                    return true;
                }

                // 通用情况
                for (let row = y; row < bottom; row++) {
                    const page = Math.floor(row / 8);
                    const pageRow = row % 8;
                    const bit = 1 << pageRow;
                    const addr = page * 128 + x;

                    if (light) {
                        for (let i = 0; i < w; i++) {
                            const currentAddr = addr + i;
                            const value = buffer.readUInt(currentAddr, 1);
                            buffer.writeUInt(currentAddr, 1, value | bit);
                        }
                    } else {
                        for (let i = 0; i < w; i++) {
                            const currentAddr = addr + i;
                            const value = buffer.readUInt(currentAddr, 1);
                            buffer.writeUInt(currentAddr, 1, value & (255 - bit));
                        }
                    }
                }
                return true;
            },
            //行主序
            drawBitmapRowMajor(x, y, width, height, bitmap) {
                if (x >= WIDTH || y >= HEIGHT) {
                    return false;
                }

                const bitmapBytesPerRow = Math.ceil(width / 8);
                let bitmapIndex = 0;

                for (let row = 0; row < height; row++) {
                    const screenY = y + row;
                    if (screenY >= HEIGHT) break;

                    for (let byteCol = 0; byteCol < bitmapBytesPerRow; byteCol++) {
                        if (bitmapIndex >= bitmap.length) break;

                        const bitmapByte = bitmap.readUInt(bitmapIndex, 1);
                        bitmapIndex++;

                        for (let bit = 0; bit < 8; bit++) {
                            const screenX = x + byteCol * 8 + (7 - bit);  // bit7在左，bit0在右
                            if (screenX >= WIDTH || screenX >= x + width) break;

                            if ((bitmapByte & (1 << bit)) != 0) {
                                this.setPixel(screenX, screenY, true);
                            }
                        }
                    }
                }
            },

            //页主序(SSD1306原生)
            drawBitmapPageMajorNA(x, y, width, height, bitmap) {
                if (x >= WIDTH || y >= HEIGHT) {
                    return false;
                }

                const pages = Math.ceil(height / 8);
                const bitmapWidthBytes = Math.ceil(width / 8);
                let bitmapIndex = 0;

                for (let page = 0; page < pages; page++) {
                    const startY = y + page * 8;

                    for (let col = 0; col < width; col++) {
                        if (bitmapIndex >= bitmap.length) break;

                        //const bitmapByte = bitmap.readUInt(bitmapIndex, 1);
                        const bitmapByte = bitmap[bitmapIndex];
                        bitmapIndex++;
                        const screenX = x + col;

                        if (screenX >= WIDTH) continue;

                        // 绘制这个字节的8个像素
                        for (let bit = 0; bit < 8; bit++) {
                            const screenY = startY + bit;
                            if (screenY >= HEIGHT || screenY >= y + height) break;

                            if ((bitmapByte & (1 << bit)) != 0) {  // bit0是顶部
                                this.setPixel(screenX, screenY, true);
                            }
                        }
                    }
                }
            },


            drawBitmapPageMajor(x, y, width, height, bitmap) {
                if (x >= WIDTH || y >= HEIGHT) {
                    return false;
                }

                // 检查是否Page对齐（y是8的倍数）
                if (y % 8 === 0) {
                    const pageStart = y / 8 * 128 + x;
                    let pageCnt = 0;
                    let bufferIndex = pageStart;

                    for (let i = 0; i < bitmap.length; i++) {
                        if (pageCnt >= width) {
                            pageCnt = 0;
                            bufferIndex = bufferIndex - width + 128; // 换到下一页
                        }

                        // 确保不越界
                        buffer.writeUInt(bufferIndex, 1, bitmap[i]); // 直接赋值


                        pageCnt++;
                        bufferIndex++;
                    }
                    return true;
                } else {
                    return this.drawBitmapPageMajorNA(x, y, width, height, bitmap);
                }
            },

            drawString(fontData, x, y, fontWidth, fontHeight, str) {
                //let fontWidth = 16;
                //let fontHeight = 16;
                for (let i = 0; i < str.length; i++) {
                    this.drawBitmapPageMajor(x + i * fontWidth, y, fontWidth, fontHeight, fontData[str.charAt(i)]);
                }
            },

            //页主序颠倒
            drawBitmapPageMajorTopBit7(x, y, width, height, bitmap) {
                if (x >= WIDTH || y >= HEIGHT) {
                    return false;
                }

                const pages = Math.ceil(height / 8);
                const bitmapWidthBytes = Math.ceil(width / 8);
                let bitmapIndex = 0;

                for (let page = 0; page < pages; page++) {
                    const startY = y + page * 8;

                    for (let col = 0; col < width; col++) {
                        if (bitmapIndex >= bitmap.length) break;

                        //const bitmapByte = bitmap.readUInt(bitmapIndex, 1);
                        const bitmapByte = bitmap[bitmapIndex];
                        bitmapIndex++;
                        const screenX = x + col;

                        if (screenX >= WIDTH) continue;

                        // 绘制这个字节的8个像素，bit7是顶部
                        for (let bit = 0; bit < 8; bit++) {
                            const screenY = startY + (7 - bit);  // bit7是顶部
                            if (screenY >= HEIGHT || screenY >= y + height) break;

                            if ((bitmapByte & (1 << bit)) != 0) {
                                this.setPixel(screenX, screenY, true);
                            }
                        }
                    }
                }
            }
        };
    }
};
