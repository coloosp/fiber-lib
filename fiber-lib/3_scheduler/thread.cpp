#include "thread.h"

#include <sys/syscall.h>
#include <iostream>
#include<unistd.h>

namespace sylar{

//thread_local是线程局部存储
static thread_local Thread* t_thread =nullptr;//线程对象指针
static thread_local std::string t_thread_name ="unkown";//当前线程名称

//获取内核真实线程id
pid_t Thread::GetThreadId()
{
    return syscall(SYS_gettid);
}

//获取线程对象指针
Thread* Thread::GetThis()
{
    return t_thread;
}


//常量引用，避免拷贝与修改
const std::string& Thread::GetName()
{
    //获取当前线程名称
    return t_thread_name;
}

//设置当前线程名称
void Thread::SetName(const std::string &name)
{

if(t_thread) //如果当前线程对象存在
{
    t_thread->m_name=name;
    //通过指针修改对象内部名称
}
t_thread_name=name;//修改线程局部线程名称

}

//实现Thread的构造函数
Thread::Thread(std::function<void()> cb,const std::string &name):
//为成员变量赋值
m_cb(cb),m_name(name)
{
    //创建线程，传入线程句柄，线程属性，入口函数，线程对象指针并返回rt判断是否创建成功
    int rt=pthread_create(&m_thread,nullptr,&Thread::run,this);
    //返回值是0说明创建成功，反之则生成错误码
    if(rt)
    {

        std::cerr <<"线程创建失败,错误码=" << rt<<"线程名称：" << name;
        //抛出异常
        throw std::logic_error("线程创建失败");

    }

    //信号量P操作，申请信号量资源
    m_semaphore.wait();
}

//实现Thread的析构函数
Thread::~Thread()
{
    //如果线程已创建未join
    if(m_thread)
    {
        //分离线程
        pthread_detach(m_thread);
        m_thread=0;//置空线程句柄
    }


}

//线程等待函数
void Thread::join()
{
    if(m_thread)
    {
        //线程等待接口
        int rt=pthread_join(m_thread,nullptr);
        if(rt)
        {
            std::cerr << "线程等待失败，错误码=" <<rt <<"线程名=" << m_name;
            throw std::logic_error("线程等待错误");
        }
        m_thread=0;

    }
}


//Thread入口函数
//void*是万能指针
void* Thread::run(void* arg)
{
    //强制转换为Thread*类型
    Thread* thread =(Thread*) arg;

    t_thread=thread;//初始化线程局部存储变量
    t_thread_name=thread->m_name;
    thread->m_id=GetThreadId();//获取内核真实线程ID

    //设置线程别名
    pthread_setname_np(pthread_self(),thread->m_name.substr(0,15).c_str());

    std::function<void()> cb;//临时回调函数

    cb.swap(thread->m_cb);//交换临时回调函数对象和线程对象中的回调函数

    thread->m_semaphore.signal();//V操作，通知semaphore信号量，唤醒主线程的wait

    //调用传入的回调函数
    cb();

    return 0;//返回空指针

}


}
