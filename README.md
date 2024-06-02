# cpp-h
Simple HTTP/s Server with static files support for Growtopia Private Server.


# Benchmark Result
For benchmark test/stress test, i use [wrk](https://github.com/wg/wrk) in Android 13 ARM64 (8 Cores, 4x2.4GHz | 4x
1.8GHz) with build command:
```cpp
g++ -O2 -o server server.cpp -lssl -lcrypto
```

### 10k Conns, 30 Seconds, 12 Threads
```sh
u0_a342@localhost ~/wrk (master)> ./wrk -c10000 -d30 -t12 --latency https://localhost
Running 30s test @ https://localhost
  12 threads and 10000 connections
  Thread Stats   Avg      Stdev     Max   +/- Stdev
    Latency   104.11ms  144.12ms   1.98s    92.63%
    Req/Sec   121.16     81.83   669.00     78.75%
  Latency Distribution
     50%   66.89ms
     75%  117.52ms
     90%  213.68ms
     99%  928.14ms
  11706 requests in 30.15s, 1.38MB read
  Socket errors: connect 12587, read 334, write 3130, timeout 342
  Non-2xx or 3xx responses: 11646
Requests/sec:    388.31
Transfer/sec:     46.99KB
```

# Special Thanks
- ChatGPT 3.5-Turbo
