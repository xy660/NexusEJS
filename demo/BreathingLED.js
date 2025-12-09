let ledc = Gpio.pwmChannels[7];
ledc.frequency = 1000;
ledc.resolution = 10;
ledc.attach(2);
while (true) {
    for (let i = 0; i < 1024; i += 10) {
        ledc.setDuty(i);
        delay(5);
    }
    delay(100);
    for (let i = 1023; i >= 0; i -= 10) {
        ledc.setDuty(i);
        delay(5);
    }
    delay(100);
}
