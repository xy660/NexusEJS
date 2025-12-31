let loopInfo = {
    stop: false
};
let tsk = runTask(() => {
    while (!loopInfo.stop) {
        //创建200个互相循环引用的对象
        for (let i = 0; i < 100; i++) {
            let obj1 = { name: "obj1" };
            let obj2 = { name: "obj2" };
            obj1.refTest = obj2;
            obj2.refTest = obj1;
        }
        //串口打印内存信息，上面的循环可能触发自动GC
        println(System.getMemoryInfo());
        
        //System.gc();
        //println(System.getMemoryInfo());
        //可以自己手动触发再次测试
        delay(1000);
    }
});
//拉高GPIO25电平停止测试
while (!Gpio.read(25)) {
    System.delay(20);
}
loopInfo.stop = true;
tsk.waitTimeout(1000);
return "ok";
