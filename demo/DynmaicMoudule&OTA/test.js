println("hello from dynmaic load!");
let counter = { value: 0 };

return {
    inc() {
        counter.value++;
        println("value:" + counter.value);
        return counter;
    }
}