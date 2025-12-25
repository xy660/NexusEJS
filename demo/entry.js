WiFi.connect("傻逼wifi","1145141919810");
while(!WiFi.isConnected()){
    System.delay(100);
    println("connecting...");
}
let resp = Http.fetch("http://192.168.137.1/test.nejs");
println("resp:" + resp);
let buf = resp.buffer();
let file = FS.open("/test.nejs",FS.FILE_WRITE);
file.write(0,buf.size,buf);
file.close();
println(FS.listDir("/").map(x=>x.name));
const res = require("/test.nejs");
println(res);
System.gc();
res = null; //to unload the test.nejs in next gc
System.gc();


