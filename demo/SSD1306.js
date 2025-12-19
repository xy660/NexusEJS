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
        const buffer = Buffer.create(1024);  // 128x64/8 = 1024字节

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

        const font8x8 = [
            // 0-31: 控制字符（用空格代替）
            [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00],  // 0: 空格(32)[[[)}"'
            [0x00, 0x00, 0x06, 0x5F, 0x5F, 0x06, 0x00, 0x00],  // 1: !(33)
            [0x00, 0x07, 0x07, 0x00, 0x07, 0x07, 0x00, 0x00],  // 2: "(34)
            [0x14, 0x7F, 0x7F, 0x14, 0x7F, 0x7F, 0x14, 0x00],  // 3: #(35)
            [0x24, 0x2E, 0x6B, 0x6B, 0x3A, 0x12, 0x00, 0x00],  // 4: $(36)
            [0x46, 0x66, 0x30, 0x18, 0x0C, 0x66, 0x62, 0x00],  // 5: %(37)
            [0x30, 0x7A, 0x4F, 0x5D, 0x37, 0x7A, 0x48, 0x00],  // 6: &(38)
            [0x00, 0x00, 0x07, 0x07, 0x00, 0x00, 0x00, 0x00],  // 7: '(39)
            [0x00, 0x00, 0x1C, 0x3E, 0x63, 0x41, 0x00, 0x00],  // 8: (40)
            [0x00, 0x00, 0x41, 0x63, 0x3E, 0x1C, 0x00, 0x00],  // 9: (41)
            [0x08, 0x2A, 0x3E, 0x1C, 0x1C, 0x3E, 0x2A, 0x08],  // 10: *(42)
            [0x08, 0x08, 0x3E, 0x3E, 0x08, 0x08, 0x00, 0x00],  // 11: +(43)
            [0x00, 0x00, 0x40, 0x60, 0x20, 0x00, 0x00, 0x00],  // 12: ,(44)
            [0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00],  // 13: -(45)
            [0x00, 0x00, 0x60, 0x60, 0x00, 0x00, 0x00, 0x00],  // 14: .(46)
            [0x40, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x02, 0x00],  // 15: /(47)

            // 数字 0-9 (ASCII 48-57)
            [0x3E, 0x7F, 0x71, 0x59, 0x4D, 0x7F, 0x3E, 0x00],  // 16: 0(48)
            [0x40, 0x42, 0x7F, 0x7F, 0x40, 0x40, 0x00, 0x00],  // 17: 1(49)
            [0x62, 0x73, 0x59, 0x49, 0x6F, 0x66, 0x00, 0x00],  // 18: 2(50)
            [0x22, 0x63, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00],  // 19: 3(51)
            [0x18, 0x1C, 0x16, 0x13, 0x7F, 0x7F, 0x10, 0x00],  // 20: 4(52)
            [0x27, 0x67, 0x45, 0x45, 0x7D, 0x39, 0x00, 0x00],  // 21: 5(53)
            [0x3C, 0x7E, 0x4B, 0x49, 0x79, 0x30, 0x00, 0x00],  // 22: 6(54)
            [0x03, 0x03, 0x71, 0x79, 0x0F, 0x07, 0x00, 0x00],  // 23: 7(55)
            [0x36, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00],  // 24: 8(56)
            [0x06, 0x4F, 0x49, 0x69, 0x3F, 0x1E, 0x00, 0x00],  // 25: 9(57)

            // 符号
            [0x00, 0x00, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00],  // 26: :(58)
            [0x00, 0x00, 0x56, 0x36, 0x00, 0x00, 0x00, 0x00],  // 27: ;(59)
            [0x08, 0x1C, 0x36, 0x63, 0x41, 0x00, 0x00, 0x00],  // 28: <(60)
            [0x14, 0x14, 0x14, 0x14, 0x14, 0x14, 0x00, 0x00],  // 29: =(61)
            [0x00, 0x41, 0x63, 0x36, 0x1C, 0x08, 0x00, 0x00],  // 30: >(62)
            [0x02, 0x03, 0x51, 0x59, 0x0F, 0x06, 0x00, 0x00],  // 31: ?(63)
            [0x3C, 0x42, 0x99, 0xA5, 0x9D, 0x42, 0x3C, 0x00],  // 32: @(64)

            // 大写字母 A-Z (ASCII 65-90)
            [0x7C, 0x7E, 0x0B, 0x09, 0x0B, 0x7E, 0x7C, 0x00],  // 33: A(65)
            [0x7F, 0x7F, 0x49, 0x49, 0x7F, 0x36, 0x00, 0x00],  // 34: B(66)
            [0x3E, 0x7F, 0x41, 0x41, 0x63, 0x22, 0x00, 0x00],  // 35: C(67)
            [0x7F, 0x7F, 0x41, 0x63, 0x3E, 0x1C, 0x00, 0x00],  // 36: D(68)
            [0x7F, 0x7F, 0x49, 0x49, 0x41, 0x41, 0x00, 0x00],  // 37: E(69)
            [0x7F, 0x7F, 0x09, 0x09, 0x01, 0x01, 0x00, 0x00],  // 38: F(70)
            [0x3E, 0x7F, 0x41, 0x49, 0x7B, 0x3A, 0x00, 0x00],  // 39: G(71)
            [0x7F, 0x7F, 0x08, 0x08, 0x7F, 0x7F, 0x00, 0x00],  // 40: H(72)
            [0x00, 0x41, 0x7F, 0x7F, 0x41, 0x00, 0x00, 0x00],  // 41: I(73)
            [0x20, 0x60, 0x40, 0x40, 0x7F, 0x3F, 0x00, 0x00],  // 42: J(74)
            [0x7F, 0x7F, 0x1C, 0x36, 0x63, 0x41, 0x00, 0x00],  // 43: K(75)
            [0x7F, 0x7F, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00],  // 44: L(76)
            [0x7F, 0x7F, 0x06, 0x0C, 0x06, 0x7F, 0x7F, 0x00],  // 45: M(77)
            [0x7F, 0x7F, 0x0E, 0x1C, 0x38, 0x7F, 0x7F, 0x00],  // 46: N(78)
            [0x3E, 0x7F, 0x41, 0x41, 0x7F, 0x3E, 0x00, 0x00],  // 47: O(79)
            [0x7F, 0x7F, 0x09, 0x09, 0x0F, 0x06, 0x00, 0x00],  // 48: P(80)
            [0x3E, 0x7F, 0x41, 0x61, 0xFF, 0xBE, 0x00, 0x00],  // 49: Q(81)
            [0x7F, 0x7F, 0x19, 0x39, 0x6F, 0x46, 0x00, 0x00],  // 50: R(82)
            [0x26, 0x6F, 0x49, 0x49, 0x7B, 0x32, 0x00, 0x00],  // 51: S(83)
            [0x01, 0x01, 0x7F, 0x7F, 0x01, 0x01, 0x00, 0x00],  // 52: T(84)
            [0x3F, 0x7F, 0x40, 0x40, 0x7F, 0x3F, 0x00, 0x00],  // 53: U(85)
            [0x1F, 0x3F, 0x60, 0x60, 0x3F, 0x1F, 0x00, 0x00],  // 54: V(86)
            [0x7F, 0x7F, 0x30, 0x18, 0x30, 0x7F, 0x7F, 0x00],  // 55: W(87)
            [0x63, 0x77, 0x1C, 0x1C, 0x77, 0x63, 0x00, 0x00],  // 56: X(88)
            [0x07, 0x0F, 0x78, 0x78, 0x0F, 0x07, 0x00, 0x00],  // 57: Y(89)
            [0x61, 0x71, 0x59, 0x4D, 0x47, 0x43, 0x00, 0x00],  // 58: Z(90)

            // 符号
            [0x00, 0x00, 0x7F, 0x7F, 0x41, 0x41, 0x00, 0x00],  // 59: 方括号(91)
            [0x02, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00],  // 60: \(92)
            [0x00, 0x00, 0x41, 0x41, 0x7F, 0x7F, 0x00, 0x00],  // 61: 右方括号(93)
            [0x00, 0x00, 0x03, 0x07, 0x04, 0x00, 0x00, 0x00],  // 62: ^(94)
            [0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x00, 0x00],  // 63: _(95)
            [0x00, 0x00, 0x01, 0x03, 0x02, 0x00, 0x00, 0x00],  // 64: `(96)

            // 小写字母 a-z (ASCII 97-122)
            [0x20, 0x74, 0x54, 0x54, 0x7C, 0x78, 0x00, 0x00],  // 65: a(97)
            [0x7F, 0x7F, 0x44, 0x44, 0x7C, 0x38, 0x00, 0x00],  // 66: b(98)
            [0x38, 0x7C, 0x44, 0x44, 0x6C, 0x28, 0x00, 0x00],  // 67: c(99)
            [0x38, 0x7C, 0x44, 0x44, 0x7F, 0x7F, 0x00, 0x00],  // 68: d(100)
            [0x38, 0x7C, 0x54, 0x54, 0x5C, 0x18, 0x00, 0x00],  // 69: e(101)
            [0x08, 0x7E, 0x7F, 0x09, 0x03, 0x02, 0x00, 0x00],  // 70: f(102)
            [0x98, 0xBC, 0xA4, 0xA4, 0xFC, 0x7C, 0x00, 0x00],  // 71: g(103)
            [0x7F, 0x7F, 0x04, 0x04, 0x7C, 0x78, 0x00, 0x00],  // 72: h(104)
            [0x00, 0x00, 0x7D, 0x7D, 0x00, 0x00, 0x00, 0x00],  // 73: i(105)
            [0x40, 0xC0, 0x80, 0x80, 0xFD, 0x7D, 0x00, 0x00],  // 74: j(106)
            [0x7F, 0x7F, 0x10, 0x38, 0x6C, 0x44, 0x00, 0x00],  // 75: k(107)
            [0x00, 0x41, 0x7F, 0x7F, 0x40, 0x00, 0x00, 0x00],  // 76: l(108)
            [0x7C, 0x7C, 0x0C, 0x18, 0x0C, 0x7C, 0x78, 0x00],  // 77: m(109)
            [0x7C, 0x7C, 0x04, 0x04, 0x7C, 0x78, 0x00, 0x00],  // 78: n(110)
            [0x38, 0x7C, 0x44, 0x44, 0x7C, 0x38, 0x00, 0x00],  // 79: o(111)
            [0xFC, 0xFC, 0x24, 0x24, 0x3C, 0x18, 0x00, 0x00],  // 80: p(112)
            [0x18, 0x3C, 0x24, 0x24, 0xFC, 0xFC, 0x00, 0x00],  // 81: q(113)
            [0x7C, 0x7C, 0x04, 0x04, 0x0C, 0x08, 0x00, 0x00],  // 82: r(114)
            [0x48, 0x5C, 0x54, 0x54, 0x74, 0x24, 0x00, 0x00],  // 83: s(115)
            [0x04, 0x3F, 0x7F, 0x44, 0x64, 0x20, 0x00, 0x00],  // 84: t(116)
            [0x3C, 0x7C, 0x40, 0x40, 0x7C, 0x7C, 0x00, 0x00],  // 85: u(117)
            [0x1C, 0x3C, 0x60, 0x60, 0x3C, 0x1C, 0x00, 0x00],  // 86: v(118)
            [0x3C, 0x7C, 0x60, 0x30, 0x60, 0x7C, 0x3C, 0x00],  // 87: w(119)
            [0x44, 0x6C, 0x38, 0x38, 0x6C, 0x44, 0x00, 0x00],  // 88: x(120)
            [0x9C, 0xBC, 0xA0, 0xA0, 0xFC, 0x7C, 0x00, 0x00],  // 89: y(121)
            [0x44, 0x64, 0x74, 0x5C, 0x4C, 0x44, 0x00, 0x00],  // 90: z(122)

            // 符号
            [0x08, 0x1C, 0x3E, 0x77, 0x41, 0x41, 0x00, 0x00],  // 91: (123)
            [0x00, 0x00, 0x7F, 0x7F, 0x00, 0x00, 0x00, 0x00],  // 92: |(124)
            [0x00, 0x41, 0x41, 0x77, 0x3E, 0x1C, 0x08, 0x00],  // 93: (125)
            [0x08, 0x0C, 0x06, 0x0C, 0x18, 0x0C, 0x06, 0x00]   // 94: ~(126)
        ];

        function getCharBitmap(ch) {
            const ascii = ch.charCodeAt(0);

            // ASCII 32-126 是可打印字符
            if (ascii >= 32 && ascii <= 126) {
                return font8x8[ascii - 32];
            }

            // 不支持的字符返回空格
            return font8x8[0];  // 空格
        }

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
            fillRect(x, y, width, height) {
                let i = 0;
                while (i < width) {
                    let j = 0;
                    while (j < height) {
                        this.setPixel(x + i, y + j, true);
                        j = j + 1;
                    }
                    i = i + 1;
                }
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
            drawBitmapPageMajor(x, y, width, height, bitmap) {
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

            drawString(x, y, str) {
                for (let i = 0; i < str.length; i++) {
                    const ascii = str.charCodeAt(i);
                    let charData;

                    if (ascii >= 32 && ascii <= 126) {
                        charData = font8x8[ascii - 32];
                    } else {
                        charData = font8x8[0];
                    }

                    this.drawBitmapPageMajor(x + i * 8, y, 8, 8, charData);
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

return SSD1306;

