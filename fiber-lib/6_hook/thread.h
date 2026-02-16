#ifndef _THREAD_H
#define _THREAD_H


#include <mutex>
#include<condition_variable>
#include<functional>
#include<string>

namespace sylar{

//信号量类
class Semaphore{
private:
std::mutex mtx;//互斥锁
std::condition_variable cv;//条件变量
int count;//信号量计数

public:

//构造函数，默认初始计数为0，explict防止隐式转换,防止C++的神人函数转化
explicit Semaphore(int count_=0) : count(count_){}


//P操作，申请信号量
void wait()
{
std::unique_lock<std::mutex> lock(mtx);//智能锁

while(count==0){//while防止虚假唤醒
    cv.wait(lock);//计数为0，进入等待状态，释放锁，线程休眠
}

//说明cv收到通知且count>0,可以申请一个信号量资源
count--;

}

//V操作，释放信号量
void signal()
{
std::unique_lock<std::mutex> lock(mtx); //加锁
count++;//释放资源，计数器加一
cv.notify_one();//如果count之前为0，唤醒P操作中的线程

}


};


//线程类
class Thread
{
public:

//构造函数声明，传入线程的回调函数和线程名
Thread(std::function<void()> cb,const std::string& name);
//析构函数声明
~Thread();

//获取线程系统id
pid_t getId() const {return m_id;}

//获取当前线程名字
const std::string& getName() const {return m_name;} 

//函数声明，等待当前线程执行完成
void join();

public:

//获取当前线程id
static pid_t GetThreadId();

//获取Thread类的指针
static Thread* GetThis();

//获取当前线程名字
static const std::string& GetName();

//设置当前线程名字
static void SetName(const std::string& name);

private:

//线程入口函数，arg为Thread类对象指针
static void* run(void* arg);

private:

pid_t m_id=-1;//线程系统ID，-1表示未创建
pthread_t m_thread=0;//线程句柄，0表示未创建

std::function<void()> m_cb;//回调函数callback
std::string m_name; //线程名字

Semaphore m_semaphore;//当前线程信号量

};



}

#endif