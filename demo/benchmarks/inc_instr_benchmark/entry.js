function benchmark() {
    let j = 1000000000;
    for (let i = 0; i < 500000; i++) {
        --j;
    }
    return j;
}

let begin = System.getUptime();
let benchRes = benchmark();
println("time=" + (System.getUptime() - begin));
println("res=" + benchRes);