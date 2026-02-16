#ifndef _SCHEDULER_H_
#define _SCHEDULER_H_

#include "fiber.h"
#include "thread.h"

#include <mutex>
#include <vector>

namespace sylar{

//调度器类
class Scheduler
{
public:

//构造函数声明，threads为工作线程数量 use_caller为是否将主线程作为工作线程
Scheduler(size_t threads=1, bool use_caller =true,const std::string& name="Scheduler");

virtual ~Scheduler();//虚函数析构函数，是为了防止父类销毁子类对象时销毁不完全而需要实现多态

//获取调度器名称
const std::string& getName() const {return m_name;}


public:

//获取正在运行的调度器指针
static Scheduler* GetThis();

protected:
//设置正在运行的调度器
void SetThis();

public:
//任务添加函数
//使用模板是因为需要加入的任务可能是线程或者回调函数
//FiberOrCb表示协程(fiber)或回调函数(callback)
template <class FiberOrCb>
void scheduleLock(FiberOrCb fc ,int thread =-1)
{
bool need_tickle; //用于标记任务队列是否为空，从而判断是否需要唤醒线程
{ //仅仅是作用域，为了方便在作用域里面加锁
    std:: lock_guard<std::mutex> lock(m_mutex);//为作用域加锁

    need_tickle =m_tasks.empty();//检查任务队列是否为空

    ScheduleTask task(fc,thread);//调度器任务对象

    if(task.fiber || task.cb) //只要存在有效线程或者有效回调函数
    {
        m_tasks.push_back(task);//将任务加入任务队列

    }
}

if(need_tickle)
{
    tickle();//唤醒空闲线程
}

}

virtual void start();//虚函数 启动线程池 启动调度器

virtual void stop();//虚函数 关闭线程池 关闭调度器

protected:

virtual void tickle(); //唤醒空闲的工作线程

virtual void run();//线程执行函数

virtual void idle();//空闲协程函数，当任务队列为空，工作线程切换到空闲协程执行，避免线程空转

virtual bool stopping();//判断调度器是否可以停止

bool hasIdleThreads(){return m_idleThreadCount>0;}

private:

//调度器任务结构体
struct ScheduleTask
{
    std::shared_ptr<Fiber> fiber;//协程智能指针
    std::function<void()> cb;//回调函数
    int thread;// 指定任务需要运行的线程id

    //无参构造函数，用于初始化任务
    ScheduleTask()
    {
        fiber=nullptr;
        cb=nullptr;
        thread =-1;
    }

    //带协程的构造函数
    ScheduleTask(std::shared_ptr<Fiber> f,int thr)
    {
        fiber=f;
        thread=thr;
    }

    //实际上传入了协程智能指针的普通指针
    ScheduleTask(std::shared_ptr<Fiber>* f,int thr)
    {
        //对f解引用，swap是智能指针自带的方法，作用是交换内部资源，交换了两个智能指针，保证引用计数不会增加
        fiber.swap(*f);
        thread=thr;
    }

    //带回调函数的构造函数
    ScheduleTask(std::function<void()> callback,int thr)
    {
        cb=callback;
        thread=thr;
    }

    //同理，传入了回调函数的指针
    ScheduleTask(std::function<void()> *callback,int thr)
    {
        cb.swap(*callback);//回调函数也有swap方法，交换内部资源
        thread=thr;
    }

    //重置任务
    void reset()
    {
        fiber=nullptr;
        cb=nullptr;
        thread=-1;
    }
};

private:
std::string m_name; //调度器名称
std::mutex m_mutex;//互斥锁
std::vector<std::shared_ptr<Thread>> m_threads; //工作线程池
std::vector<ScheduleTask> m_tasks;//任务队列
std::vector<int> m_threadIds; //工作线程id集合
size_t m_threadCount =0;//工作线程总数
std::atomic<size_t> m_activeThreadCount {0};//活跃线程数
std::atomic<size_t> m_idleThreadCount{0};//空闲线程数

bool m_useCaller;//是否将主线程作为工作线程
std::shared_ptr<Fiber> m_schedulerFiber;//调度协程
int m_rootThread =-1;//主线程id
bool m_stopping =false; //调度器停止标记


};


}

# endif