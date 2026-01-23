let val = 0;
vtStart(() => {
    while (true) {
        println("hha");
        println(val++);
        vtDelay(300);
    }
});

vtDelay(3000);
vtSetScheduleEnabled(false); //关闭调度器，等效于获取当先线程VT调度器的互斥锁
//此时可进行共享资源访问
val = 123456;
vtSetScheduleEnabled(true); //重新开启调度器，恢复其他VT的正常执行
println("恢复调度");

//delay(12345); 不建议使用delay，因为delay属于native层阻塞调用，会导致其他VT无法被调度，如果需要阻塞IO请使用基于RTOS的runTask
