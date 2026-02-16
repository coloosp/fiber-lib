#ifndef _COROUTINE_H_
#define _COROUTINE_H_

#include <iostream>
#include <memory>
#include <atomic>
#include <functional>
#include <cassert>
#include <ucontext.h>
#include <unistd.h>
#include <mutex>

namespace sylar{

class Fiber : public std::enable_shared_from_this<Fiber>
{
public:
enum State //枚举，定义协程三种状态
{
READY, //准备
RUNNING, //正在运行
TERM //运行结束
};

private:
Fiber();
//私有构造函数，禁止外部调用，只能使用本类中的GetThis()静态函数调用
//用于创建主协程



public:
Fiber(std::function<void()> cb,size_t stacksize =0,bool run_in_scheduler =true);
//构造函数，用于创建子协程
//参数1是传入的业务回调函数，参数2为协程栈的大小，默认为0使用后续函数实现的默认大小
//参数3是否交给协程调度器执行

~Fiber();//析构函数声明

void reset(std::function<void()> cb);//重置回调函数声明

void resume();//恢复/继续当前协程执行函数声明，获得CPU执行权

void yield();//暂停当前协程函数声明，让出CPU执行权

//获取协程id
uint64_t getId() const {return m_id;}

//获取当前协程运行状态
State getState() const {return m_state;}


public:
//设置当前运行的协程
static void SetThis(Fiber *f);

//获取当前协程的智能指针
static std::shared_ptr<Fiber> GetThis();

//设置当前线程的调度协程
static void SetSchedulerFiber(Fiber *f);

//获取当前正在运行的协程的唯一ID
static uint64_t GetFiberId();

//协程的入口函数
static void MainFunc();

private:
uint64_t m_id=0;//协程ID，初始设置为0

uint32_t m_stacksize =0;//协程运行时栈内存的大小

State m_state =READY;//协程运行状态，初始为准备状态

ucontext_t m_ctx;//协程上下文

void* m_stack =nullptr;//协程栈内存的起始地址指针，类型为万能指针

std::function<void()> m_cb;//协程要执行的回调函数

bool m_runInScheduler;//是否将当前协程交给调度协程


public:
std::mutex m_mutex;//互斥锁

};

}

#endif