# fiber-lib
 基于 C++ 的用户态协程调度与异步 IO 框架
# 项目简介
一个用户态协程框架，参考了sylar项目的设计思想。基于C++开发，通过实现协程上下文切换，调度器和Hook机制，让开发者可以使用同步的代码风格，编写高性能的异步应用。
# 核心技术栈
*编写语言：C++11/14
*协程实现：基于linux的ucontext_t实现对有栈非对称协程
*IO多路复用：epoll
*并发模型：协程调度器+线程池
*关键特性：Hook机制，事件触发，智能指针，定时器
# 模块介绍
*线程模块：封装了pthread,互斥量，信号量，读写锁
*协程模块：基于ucontext_t实现了非对称协程，设计了三种协程状态，可以让子协程和线程主协程或者调度协程之间相互切换
*定时器：基于set时间堆实现的定时器功能，支持对定时事件的添加，删除与更新
*协程调度：在基本的协程调度器基础上结合epoll和定时器实现了IO协程调度，支持IO事件和定时事件的注册和回调
*Hook:基于IO协程调度器对sleep，socket，fd操作等系列系统函数进行hook封装实现阻塞调用的异步
#性能测试：
## 测试硬件环境
*操作系统：Ubuntu 22.04.5 LTS (Jammy Jellyfish)
*内核版本：5.15.0-142-generic
*处理器：2vCPU
*内存：1 GiB
*存储：30 GiB ESSD云盘
*网络环境：中国香港区域云服务器。公网IP ：47.86.55.223

## 测试工具与命令
使用 ApacheBench（`ab`）工具对服务器进行性能测试，测试命令如下：

```bash
ab -n 10000 -c 500 http://47.86.55.223:8080/
```
## 测试结果
Server Software: 
Server Hostname:        47.86.55.223

Server Port:            8080

Document Path:          /

Document Length:        10 bytes

Concurrency Level:      500

Time taken for tests:   10.371 seconds


Complete requests:      10000
Failed requests:        0
Total transferred:      990000 bytes
HTML transferred:       100000 bytes
Requests per second:    964.27 [#/sec] (mean)
Time per request:       518.529 [ms] (mean)
Time per request:       1.037 [ms] (mean, across all concurrent requests)
Transfer rate:          93.22 [Kbytes/sec] received

Connection Times (ms)

              min  mean[+/-sd] median   max
Connect:        0    3   7.7      1      52
Processing:    19  505  98.4    500     727
Waiting:        2  505  98.4    500     727
Total:         54  508  93.9    501     728

Percentage of the requests served within a certain time (ms)
  50%    501
  66%    539
  75%    573
  80%    586
  90%    628
  95%    657
  98%    687
  99%    700
 100%    728 (longest request)
