const SSD1306 = require("/SSD1306.nejs");

const oled = SSD1306.create(5, 4, 2);

//test draw a rect
oled.clear();
oled.drawRect(40, 20, 48, 24);
oled.commit();
delay(1000);

oled.clear();

//test draw string
oled.drawString(0, 0, "NexusEJS SSD1306");
oled.drawString(0, 10, "Hello world!!");

oled.commit();

delay(3000);

println("测试完成");

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

while (true) {
    cnt.value++;
    delay(1000);
}





