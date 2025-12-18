const LCD_1602 = {
    create(sda, scl, addr) {
        const i2c = I2C.init(sda, scl, 100000);
        const BACKLIGHT = 0x08;
        const EN = 0x04;
        const RS = 0x01;
        
        function write4bits(data) {
            i2c.write(addr, data | BACKLIGHT | EN);
            delay(1);
            i2c.write(addr, data | BACKLIGHT);
            delay(5);
        }
        
        function sendCommand(cmd) {
            let high = cmd & 0xF0;
            let low = (cmd << 4) & 0xF0;
            
            write4bits(high | BACKLIGHT);
            write4bits(low | BACKLIGHT);
        }
        
        function sendData(data) {
            let high = data & 0xF0;
            let low = (data << 4) & 0xF0;
            
            write4bits(high | BACKLIGHT | RS);
            write4bits(low | BACKLIGHT | RS);
        }
        
        i2c.write(addr, BACKLIGHT);
        delay(1000);
        
        for (let i = 0; i < 3; i++) {
            write4bits(0x30 | BACKLIGHT);
        }
        
        write4bits(0x20 | BACKLIGHT);
        sendCommand(0x28);
        sendCommand(0x0C);
        sendCommand(0x01);
        sendCommand(0x06);
        
        println("LCD初始化完成");
        
        // 返回接口
        return {
            clear() {
                sendCommand(0x01);
                delay(5);
            },
            
            setCursor(col, row) {
                let addr = 0x80;
                if (row == 1) {
                    addr = 0xC0;
                }
                sendCommand(addr + col);
            },
            
            print(text) {
                for (let i = 0; i < text.length; i++) {
                    sendData(text.charCodeAt(i));
                }
            },
            
            test() {
                this.clear();
                this.setCursor(0, 0);
                this.print("Contrast Test");
                this.setCursor(0, 1);
                for (let i = 0; i < 16; i++) {
                    sendData(0xFF);
                }
            },
            
            close() {
                i2c.close();
            }
        };
    }
};

return LCD_1602;
