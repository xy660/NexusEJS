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

global.motor = Servo.create(16,2);

WiFi.connect("your_wifi","your_password");
while(!WiFi.isConnected()){
    delay(500);
    println("connecting..");
}
println("current device ip is " + WiFi.localIP());
let web = Http.createServer(80);
web.mapGet("/servo",args=>{
    let degree = Number.parseInt(args.deg);
    println("deg=" + degree);
    if(degree <= 180 && degree >= 0){
        motor.setAngle(degree);
        return "success";
    }else{
        return "the degree is invaild:" + degree;
    }
});
web.begin();


//block the main task
while(true){
    delay(99999);
}
