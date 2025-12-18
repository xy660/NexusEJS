const LCD_1602 = require("/LCD1602.nejs");
// 使用
const lcd = LCD_1602.create(21, 22, 39);
lcd.clear();
lcd.setCursor(0, 0);
lcd.print("NEXUS EJS");
let i = 0;
while (true) {
    lcd.setCursor(0, 1);
    lcd.print("time:" + i);
    i++;
    delay(1000);
}
