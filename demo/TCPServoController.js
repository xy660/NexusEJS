let Servo = {
    create(pin, channel) {
        let pwm = Gpio.pwmChannels[channel];
        pwm.frequency = 50;
        pwm.resolution = 10;
        pwm.attach(pin);
        return {
            pwm: pwm,
            setAngle(angle) {
                let duty = 26 + (angle * 1023 * 2) / (180 * 20);
                this.pwm.setDuty(duty);
            }
        }
    }
};
let buf = Buffer.create(4);
global.servo = Servo.create(16, 3);
global.servoDegree = 0;
delay(1000);
//Create an independent task to achieve smoother rotation.
runTask(param => {
    let cur = 0;
    while (true) {
        if (cur < global.servoDegree) {
            cur+=0.5;
        } else if (cur > global.servoDegree) {
            cur-=0.5;
        }

        global.servo.setAngle(cur);
        delay(10);
    }
}, null);

WiFi.connect("your_wifi", "your_password");
while (!WiFi.isConnected()) {
    delay(500);
}
println("my ip is " + WiFi.localIP());
println(Socket);
let connection = null;

while (true) {
    try {
        //todo: change to right ip and port
        connection = Socket.connectTCP("192.168.137.1", 8080);
        break;
    } catch (ex) {
        println("ex=" + ex);
    }
    delay(1000);
}



while (true) {
    connection.recvSize(buf, 0, 4);

    let tmp = buf.readUInt(0, 4);

    servoDegree = tmp;

    println("target=" + servoDegree);
}
