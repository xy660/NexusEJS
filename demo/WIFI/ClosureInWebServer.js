WiFi.connect("your_wifi","your_password");
while(!WiFi.isConnected()){
    Gpio.set(2,true); //displa wifi state with led
    delay(200);
    Gpio.set(2,false);
    delay(200);
}
Gpio.set(2,true);
let server = Http.createServer(80);
let cnt = 0;
let obj = {
    value:0,
    inc(){
        this.value++;
    }
};
server.mapGet("/inc",()=>{
    println(_clos); //print the closure object to serial
    cnt++; //The cnt variable is a value copy within the closure's environment object; modifications do not affect the outer scope
    obj.inc(); //The obj variable captures a reference; modifying the object affects the outer scope
    return "cnt=" + cnt + "\r\nobj=" + obj;
});

server.begin();

println("server begin() server=" + server);

while(true){
    delay(10000);
    println("main() view: cnt=" + cnt + "  obj=" + obj);
}
