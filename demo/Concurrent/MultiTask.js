let counter = {cnt:0};
for(let i = 0;i < 50;i++){
    let p = runTask(()=>{
        println("task" + i);
        delay(500);
        lock(counter){ //lock statement use native mutex to protect access
            counter.cnt++;
        };
        return counter.cnt;
    })
}

delay(5000);

return counter.cnt; //the result is 50
