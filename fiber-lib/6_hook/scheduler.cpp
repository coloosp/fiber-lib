#include "scheduler.h"

static bool debug =false; //调试

namespace sylar{

static thread_local Scheduler* t_scheduler =nullptr;//线程局部变量，当前线程调度器裸指针

//获取当前线程调度器指针
Scheduler* Scheduler::GetThis()
{
    return t_scheduler;
}

//设置当前线程调度器
void Scheduler::SetThis()
{
    t_scheduler=this;//绑定当前线程调度器
}


//构造函数 传入工作线程数量，是否将主线程当做工作线程，线程名
Scheduler::Scheduler(size_t threads,bool use_caller,const std::string &name):
m_useCaller(use_caller),m_name(name)
{
    //断言，判断工作线程必须大于0，当前线程并没有绑定任何调度器(避免重复初始化)
    assert(threads >0 && Scheduler::GetThis()==nullptr);

    SetThis();//设置当前调度器对象

    Thread::SetName(m_name);//设置当前线程名为调度器名称

    //使用主线程为工作线程
    if(use_caller)
    {
        // 因为主线程担任一个工作线程的责任，所以工作线程数减一
        threads --;

        //创建主协程，Fiber类里面的GetThis在首次调用时会创建主协程
        Fiber::GetThis();
        // m_schedulerFiber是调度协程智能指针，使用了智能指针的reset函数，创建调度器专属调度协程
        m_schedulerFiber.reset(new Fiber(std::bind(&Scheduler::run,this),0,false));

        Fiber::SetSchedulerFiber(m_schedulerFiber.get());//获取调度协程裸指针，设置为当前协程的调度协程

        m_rootThread=Thread::GetThreadId();//记录主线程ID

        m_threadIds.push_back(m_rootThread);//加入工作线程ID集合
    }

    m_threadCount =threads;//初始化调度器的工作线程总数

    if(debug) 
        std::cout << "构造调度器成功\n";

}

//调度器析构函数
Scheduler::~Scheduler()
{

//调度器必须处于停止状态才能析构
assert(stopping()==true);

if(GetThis()==this) //如果当前线程的调度器是当前实例
{
     //将线程局部变量的调度器指针置空，解除线程与调度器的绑定
    t_scheduler=nullptr;
}

if(debug) 
    std::cout <<"调度器析构成功\n";
}

//启动调度器
void Scheduler::start()
{

    std::lock_guard<std::mutex> lock(m_mutex); //线程锁

    if(m_stopping) //调度器已经暂停不允许启动
    {
        std::cerr<<"调度器已经暂停" << std::endl;
        return ;
    }

    assert(m_threads.empty()); //工作线程池必须为空，防止重复创建多个线程池

    m_threads.resize(m_threadCount);//线程池调整为工作线程数

    //遍历工作线程池
    for(size_t i=0;i<m_threadCount;i++)
    {
        //创建工作线程，重置智能指针的管理内存,传入了调度器的运行函数run作为线程的回调函数参数
        m_threads[i].reset(new Thread(std::bind(&Scheduler::run,this),m_name+"_"+std::to_string(i)));

        //加入工作线程ID集合
        m_threadIds.push_back(m_threads[i]->getId());
    }
    if(debug) 
        std::cout<<"调度器启动成功\n";
}

//运行函数，所有工作线程的入口函数(以及作为工作线程的主线程)
void Scheduler::run()
{
    int thread_id =Thread::GetThreadId();//获取当前线程id，GetThreadId是类中的静态函数

    if(debug)
        std::cout <<"当前执行调度器run函数的线程id为 :" <<thread_id <<std::endl;

        //set_hook_enable(true);//暂时不知道功能

        SetThis();//设置调度器对象

        if(thread_id!=m_rootThread) //不是主线程,可能没有创建主协程,主线程的主协程在构造调度器时已经创建
        {
            Fiber::GetThis();//初始调用GetThis会创建主协程

        }

        //创建空闲协程，绑定idle成员函数
        std::shared_ptr<Fiber> idle_fiber =std::make_shared<Fiber>(std::bind(&Scheduler::idle,this));

        ScheduleTask task;//定义当前任务对象

        while(true)
        {
            task.reset();//重置任务对象，防止上一次任务残留

            bool tickle_me=false; //标记是否唤醒了其他线程进行任务调度

            {//添加作用域便于加锁
                std::lock_guard<std::mutex> lock(m_mutex);//互斥锁

                auto it =m_tasks.begin();//迭代器

                while(it!=m_tasks.end())
                {
                    //为了查找是否存在其他线程的任务存在，-1是一个默认值
                    if(it->thread!=-1 && it->thread!=thread_id)
                    {
                        it++;
                        tickle_me =true;//出现其他线程的任务,修改标记
                        continue; //继续查询，先不执行下面内容
                    }

                    assert(it->fiber || it->cb);//需要存在有效协程或者有效回调函数
                    task =*it;//获取找到的当前任务
                    m_tasks.erase(it);//弹出当前任务
                    m_activeThreadCount++;//活跃线程数+1，当前线程开始处理任务
                    break;;//已经找到当前任务不需要继续查找
                }
                tickle_me =tickle_me || (it!=m_tasks.end());//更新tickle_me标记
            }
                if(tickle_me)
                {
                    tickle();//使用tickle函数唤醒其他线程
                }

            //任务是协程对象
            if(task.fiber)
            {
                {//添加作用域便于加锁
                    std::lock_guard <std::mutex> lock (task.fiber->m_mutex);//互斥锁
                    if(task.fiber->getState()!=Fiber::TERM) //保证不是处于终止状态的协程
                    {
                        task.fiber->resume();//恢复协程执行
                    }
                }

                m_activeThreadCount--;//协程运行结束后活跃线程-1
                task.reset();//重置任务对象

            }

            //任务是回调对象
            else if(task.cb)
            {
                //对于回调函数的处理，实际上是封装成协程对象，按照协程的形式处理
                //以下内容同上
                std::shared_ptr<Fiber> cb_fiber =std::make_shared<Fiber>(task.cb);
                {  
                    std::lock_guard<std::mutex> lock(cb_fiber->m_mutex);
                    cb_fiber->resume();
                }
                m_activeThreadCount--;//活跃线程数-1
                task.reset();
            }

            //无有效任务时,实际上应该是任务队列为空时task为初始化
            else
            {
                //会切换到空闲协程
                //当对应空闲协程进入终止态即结束死循环
                if(idle_fiber->getState()==Fiber::TERM)
                {
                    if(debug)
                        std:: cout <<"调度器run函数结束于线程 :" <<thread_id <<std::endl;
                        break;
                }
                m_idleThreadCount++;
                idle_fiber->resume();//切换到空闲协程
                m_idleThreadCount--;
            }

        } //while(true)死循环

} //run函数

//停止调度器
void Scheduler::stop()
{
    if(debug)
        std::cout << "调度器stop函数开始于 线程:" <<Thread::GetThreadId() <<std::endl;

        //调度器已经处于可停止状态返回即可，防止重复停止
    if(stopping())
    {
        return ;
    }

    m_stopping =true;//标记为停止状态


    assert(GetThis()==this);

    for(size_t i=0;i < m_threadCount;i++)
    {
        tickle();
    }

    //存在调度协程
    if(m_schedulerFiber)
    {
        tickle();
    }

    //貌似tickle()是一次性唤醒所有空闲线程，这里只是稳妥起见在重复唤醒


    //存在调度协程
    if(m_schedulerFiber)
    {
        m_schedulerFiber->resume();

        if(debug)
            std::cout <<"调度协程结束于线程 :" <<Thread::GetThreadId() <<std::endl;
    }

    //临时线程容器
    std::vector<std::shared_ptr<Thread>> thrs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        thrs.swap(m_threads);//交换临时线程容器和工作线程池，使工作线程池清空
    }

    //等待所有工作线程执行完成
    for(auto &i : thrs)
    {
        i->join();//等待当前线程执行完成
    }

    if(debug)
        std:: cout <<"调度器stop函数结束于线程 :" <<Thread::GetThreadId() <<std::endl;

}

void Scheduler::tickle(){
    //暂不实现，根据具体情况实现具体唤醒函数
    //tickle函数的作用是实际上是对所有线程的无差别通知，总之是改变处于空闲阻塞状态的线程
}


//空闲协程执行逻辑,切换到空闲协程之后，防止协程进入while死循环空转
void Scheduler::idle()
{
    //只要调度器不停止就一直执行
    while(!stopping())
    {
        if(debug) 
            std::cout <<"调度器idle,休眠在线程 :" << Thread::GetThreadId() <<std::endl;
        
            sleep(1);//休眠一秒，让出CPU执行权

            Fiber::GetThis()->yield();//空闲协程需要让出执行权
    }
}

//判断调度器是否可以停止
bool Scheduler::stopping()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    
    //需要满足三个条件 停止标记、任务队列为空、无活跃线程
    return m_stopping && m_tasks.empty()&& m_activeThreadCount==0;

}


}