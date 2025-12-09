let counter = {cnt:0};
for(let i = 0;i < 50;i++){
    let p = runTask(x=>{
        println("task" + x.id);
        delay(500);
        lock(x.obj){ //lock statement use native mutex to protect access
            x.obj.cnt++;
        };
        return x.obj.cnt;
    },{id:i,obj:counter})
}

return counter.cnt;