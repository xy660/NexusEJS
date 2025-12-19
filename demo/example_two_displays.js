//the demo require the LCD_1602 driver and SSD1306 driver that you can find the source from ./LCD_1602 and ./SSD1306 directory.

const SSD1306 = require("/SSD1306.nejs");
const LCD_1602 = require("/LCD1602.nejs");

const oled = SSD1306.create(5, 4, 2);

const lcd = LCD_1602.create(21, 22, 39);

//test draw a rect
oled.clear();
oled.drawRect(33, 20, 66, 10);

//test draw string
oled.drawString(34, 21, "NexusEJS");

oled.commit();

lcd.clear();
lcd.setCursor(0, 0);
lcd.print("NexusEJS");

delay(3000);


gc();

//boxed number to capture reference by closure
let cnt = { value: 0 };

runTask(() => {
    while (true) {
        oled.clear();
        oled.drawString(0, 0, "NexusEJS Timer");
        oled.drawLine(0, 10, 127, 10);
        oled.drawString(0, 20, "Time:" + cnt.value);
        oled.commit();
    }
});

runTask(() => {
    while (true) {
        lcd.setCursor(0, 1);
        lcd.print("Time:" + cnt.value);
        delay(400);
    }
});

//counting in main thread
while (true) {
    cnt.value++;
    delay(1000);
}





