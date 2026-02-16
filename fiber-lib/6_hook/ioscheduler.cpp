#include <unistd.h>
#include <sys/epoll.h>
#include<fcntl.h>
#include<cstring>

#include "ioscheduler.h"

static bool debug =false;

namespace sylar{

//获取当前线程的IOManager实例指针
IOManager* IOManager::GetThis()
{
    //将线程调度器中的实力指针强制转换为IOManager的实例指针，因为在获取实例指针时需要使用Scheduler的线程局部变量
    //dynamic_cast是安全的向下转型
    return dynamic_cast<IOManager*> (Scheduler::GetThis());
}

//获取指定事件对应的事件上下文
IOManager::FdContext::EventContext&  IOManager::FdContext::getEventContext(Event event)
{
    assert(event==READ ||event==WRITE);//我们只有读写事件
    switch(event)
    {
        case READ:
            return read;//读事件上下文
        case WRITE:
            return write;//写事件上下文

    }

    throw  std::invalid_argument("不支持的事件类型");

}

//重置事件上下文
void IOManager::FdContext::resetEventContext(EventContext &ctx)
{
    ctx.scheduler =nullptr; //清空关联的调度器
    ctx.fiber.reset();//释放协程
    ctx.cb=nullptr;//清空回调函数

}

//触发指定事件
void IOManager::FdContext::triggerEvent(IOManager::Event event){
    assert(events & event); //保证fd已注册该事件,按位与运算

    events =(Event)(events & ~event);//从此fd已经注册的时间中删除该事件

    EventContext& ctx =getEventContext(event);
    //如果你还记得的话，我们的协程调度器可以处理协程和回调函数两种类型
    if(ctx.cb)
    {
        ctx.scheduler->scheduleLock(&ctx.cb);//添加回调函数进入任务队列
    }
    else
    {
        ctx.scheduler->scheduleLock(&ctx.fiber);//添加协程进入任务队列
    }

    resetEventContext(ctx);//重置事件上下文
    return;

}

IOManager::IOManager(size_t threads,bool use_caller,const std::string &name):
Scheduler(threads,use_caller,name),TimerManager() //封装了线程调度器和定时器管理器
{
    m_epfd =epoll_create(5000); //创建epoll实例并获取epoll文件描述符,这里的参数毫无意义，大于0即可

    assert(m_epfd >0);//成功创建后会返回非负整数

    //rt是用来判断以下操作是否成功的返回值,0表示成功，否则返回错误码
    int  rt= pipe(m_tickleFds);//创建管道，实际上是我们的普通数组绑定为管道资源

    assert(!rt);

    //将管道监听注册到epoll上
    epoll_event event;
    event.events =EPOLLIN | EPOLLET;//EPOLLIN表示读事件，EPOLLET表示边缘触发
    event.data.fd =m_tickleFds[0]; //绑定管道读端,检测读端是否有数据

    rt =fcntl(m_tickleFds[0],F_SETFL,O_NONBLOCK); //将管道读端设置为非阻塞模式

    assert(!rt);

    rt=epoll_ctl(m_epfd,EPOLL_CTL_ADD,m_tickleFds[0],&event);
    assert(!rt);

    contextResize(32);//初始化fd上下文数组

    start();//启动调度器

}

//析构函数
IOManager::~IOManager(){

stop();//停止调度器
//关闭epoll句柄，管道读写端
close(m_epfd);
close(m_tickleFds[0]);
close(m_tickleFds[1]);

//释放所有fd上下文
for(size_t i=0;i< m_fdContexts.size();++i)
{
    if(m_fdContexts[i])
    {
        delete m_fdContexts[i];
    }
}

}

//调整fd上下文数组大小
void IOManager::contextResize(size_t size)
{
    m_fdContexts.resize(size);

    for(size_t i=0;i < m_fdContexts.size();++i)
    {
        if(m_fdContexts[i]==nullptr)//新扩容的地方
        {
            m_fdContexts[i]=new FdContext();
            m_fdContexts[i]->fd=i;//将文件描述符的编号赋给fd
        }
    }

}

//为指定的 fd 注册（添加）一个需要 epoll 监听的 IO 事件
int IOManager::addEvent(int fd,Event event,std::function<void()> cb)
{
    FdContext *fd_ctx=nullptr;//初始化文件上下文

    //共享读锁
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);

    if((int)m_fdContexts.size() >fd) //当前fd要在数组范围内
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();//获取上下文后释放锁
    }
    else
    {
        read_lock.unlock();//没找到也先释放锁

        std::unique_lock<std::shared_mutex> write_lock(m_mutex);//加写锁    
        contextResize(fd *1.5);//扩容到fd的1.5倍
        fd_ctx=m_fdContexts[fd];//获取fd的上下文
    }

    std::lock_guard <std::mutex> lock(fd_ctx->mutex);//加fd上下文互斥锁

    //检查该事件是否已经注册避免重复添加
    if(fd_ctx->events &event)
    {
        return -1;
    }

    //判断事件是否存在，存在则修改(mod),不存在则添加(add)
    int op=fd_ctx->events ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

    //epoll_event结构体，绑定该文件的上下文
    epoll_event epevent;
    epevent.events =EPOLLET |fd_ctx->events |event;
    epevent.data.ptr =fd_ctx;//监控对象为该文件的上下文

    //调用epoll_ctl添加或修改事件
    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr <<"addEvent失败,调用epoll_ctl失败" << strerror(errno) <<std::endl;
        return -1;
    }

    ++m_pendingEventCount;//待处理IO事件数+1

    //使用或运算表示添加，使用与运算表示检查包含
    fd_ctx ->events =(Event)(fd_ctx->events |event);//更新fd上下文的已注册事件

    FdContext::EventContext& event_ctx =fd_ctx ->getEventContext(event);//更新事件上下文

    assert(!event_ctx.scheduler && !event_ctx.fiber && !event_ctx.cb);//保证事件上下文为初始状态，未被使用

    event_ctx.scheduler =Scheduler::GetThis();//绑定当前fd的调度器

    if(cb)//传入了回调函数
    {
        event_ctx.cb.swap(cb);//swap是为了避免拷贝
    }
    else //这里相当有趣，如果我们并没有传入回调函数，会默认执行对应协程的回调函数，也就是恢复当前协程的任务
    {
        event_ctx.fiber =Fiber::GetThis();//获取当前执行的协程
        assert(event_ctx.fiber->getState() == Fiber::RUNNING);//只允许绑定运行状态的协程
    }

    return 0;

}

//删除fd的指定事件，删除但不触发
bool IOManager::delEvent(int fd,Event event)
{
    FdContext *fd_ctx=nullptr;//初始化文件上下文

    //共享读锁
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    //在范围内查找fd上下文
    if((int)m_fdContexts.size() >fd)
    {
        fd_ctx=m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;//在数组中不存在这个文件
    }
    //fd上下文锁
    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    //检查是否包含对应事件
    if(!(fd_ctx->events & event))
    {
        return false;
    }

    Event new_events =(Event)(fd_ctx->events & ~event);//更新删除对应事件以后fd的事件
    //根据是否有剩余事件选择操作
    int op=new_events ? EPOLL_CTL_MOD : EPOLL_CTL_DEL;
    
    //修改epoll_event结构体的监听事件
    epoll_event epevent;
    epevent.events =EPOLLET | new_events;//边缘触发+剩余事件
    epevent.data.ptr =fd_ctx;

    int rt =epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr <<"delEvent失败,epoll_ctl失败" <<strerror(errno) <<std::endl;
        return -1;
    }

    --m_pendingEventCount;//待处理IO事件数-1

    fd_ctx->events= new_events;//更新fd上下文的事件
    
    //获取并重置事件上下文
    FdContext::EventContext& event_ctx =fd_ctx->getEventContext(event);
    fd_ctx->resetEventContext(event_ctx);
    return true;

}

//取消fd指定事件并触发回调
bool  IOManager::cancelEvent(int fd,Event event)
{
    FdContext *fd_ctx=nullptr;//初始化文件上下文

    //共享读锁
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    //查找对应上下文
    if((int)m_fdContexts.size() >fd)
    {
        fd_ctx =m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    //检查是否包含该事件
    if(!(fd_ctx->events & event))
    {
        return false;
    }

    Event new_events =(Event)(fd_ctx->events & ~event);//更新删除后的剩余事件
    int op =new_events ? EPOLL_CTL_MOD :EPOLL_CTL_DEL;//根据剩余事件为空还是还存在

    epoll_event epevent;
    epevent.events =EPOLLET | new_events;
    epevent.data.ptr =fd_ctx;

    int rt =epoll_ctl(m_epfd,op,fd,&epevent);//注销事件
    if(rt)
    {
        std::cerr <<"cancelEvent失败,epoll_ctl失败:" <<strerror(errno) <<std::endl;
        return -1;
    }

    --m_pendingEventCount;//待处理IO事件数-1

    fd_ctx->triggerEvent(event);//触发事件回调，这是和上一个函数唯一区别
    return true;

}

//取消fd的所有事件并触发回调
bool IOManager::cancelAll(int fd)
{
    //经典流程
    FdContext *fd_ctx =nullptr;
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    if((int)m_fdContexts.size() >fd)
    {
        fd_ctx =m_fdContexts[fd];
        read_lock.unlock();
    }
    else
    {
        read_lock.unlock();
        return false;
    }

    std::lock_guard<std::mutex> lock(fd_ctx->mutex);

    //如果fd无事件就返回
    if(!fd_ctx->events)
    {
        return false;
    }

    int op=EPOLL_CTL_DEL;//因为要全部取消所以操作只有删除
    epoll_event epevent;
    epevent.events=0;
    epevent.data.ptr =fd_ctx;

    int rt=epoll_ctl(m_epfd,op,fd,&epevent);
    if(rt)
    {
        std::cerr <<"cancelAll失败,epoll_ctl失败:" <<strerror(errno) <<std::endl;
        return -1;
    }

    //如果有读事件
    if(fd_ctx->events & READ)
    {
        fd_ctx->triggerEvent(READ);//触发函数会把引用返回值，所以不用担心删除问题
        --m_pendingEventCount;
    }

    if(fd_ctx->events & WRITE)
    {
        fd_ctx->triggerEvent(WRITE);
        --m_pendingEventCount;
    }

    assert(fd_ctx->events==0);//当所有事件已经注销后
    return true;

}

//重写Scheduler::tickle,唤醒idle协程
void IOManager::tickle()
{
    if(!hasIdleThreads()) //没有空闲线程不需要唤醒
    {
        return;
    }
    //向管道写端写入1个字节即可唤醒线程切换到工作协程
    //写入什么字节都无所谓，只要写入字节到管道中就会结束epoll_wait
    int rt =write(m_tickleFds[1],"T",1); 
    assert(rt==1);
}

//重写Scheduler::stopping,判断IOManager是否可以停止
bool IOManager::stopping()
{
    uint64_t timeout =getNextTimer();//判断事件堆里面是否存在定时器，没有的情况会返回~0ull
    //需要满足无定时器，无待处理IO事件并且协程调度器停止标志
    return timeout==~0ull && m_pendingEventCount==0 && Scheduler::stopping();
}

//空闲函数
void IOManager::idle()
{
    static const uint64_t MAX_EVENTS =256; //最大事件数

    //创建epoll_event数组并使用智能指针管理
    std::unique_ptr<epoll_event[]> events(new epoll_event[MAX_EVENTS]);

    while(true) //进入死循环
    {
        if(debug) 
            std::cout <<"空闲协程运行在线程:" <<Thread::GetThreadId() <<std::endl;
        
        if(stopping()) //满足IO调度器停止条件时离开死循环
        {
            if(debug)
                std::cout <<"调度器为" <<getName() <<"空闲协程在线程:" <<Thread::GetThreadId() <<std::endl;
            break;
        }


        int rt =0;
        while(true)
        {
            static const uint64_t MAX_TIMEOUT =5000;//最大超时时间

            uint64_t next_timeout =getNextTimer();//下一个定时器的超时时间

            next_timeout =std::min(next_timeout,MAX_TIMEOUT);//取较小值

            //替换原本的休眠函数为阻塞等待事件，这里的get函数是为了获取events数组智能指针的原指针
            //rt表示本次调用实际获取到的就绪 IO 事件数量
            //epoll_wait会把接收到的就绪事件填入events中
            rt=epoll_wait(m_epfd,events.get(),MAX_EVENTS,(int)next_timeout);
            //EINTR指系统调用被信号中断，重试epoll_wait
            if(rt <0 && errno ==EINTR)
            {
                continue;
            }
            else//如果rt接收到事件(rt>0) 或者定时器超时(rt=0)都会结束
            {
                break;
            }
        }//while

        //在结束阻塞后先处理两种任务1.超时触发的任务2.新就绪IO的任务

        std::vector<std::function<void()>>  cbs;//回调函数数组
        listExpiredCb(cbs);//获取所有超时定时器回调并存储在cbs中
        //处理超时定时器事件
        if(!cbs.empty())
        {
            for(const auto&cb: cbs)
            {
                scheduleLock(cb);//加入任务
            }
            cbs.clear();
        }

        //处理就绪的IO事件
        for(int i=0;i< rt;++i)
        {
            epoll_event& event =events[i];//引用原数据避免拷贝

            //检查当前事件是否是由tickle唤醒的事件
            //因为tickle唤醒的事件对应的fd是m_tickleFds
            if(event.data.fd ==m_tickleFds[0])
            {
                uint8_t dummy[256];//临时缓冲区
                while(read(m_tickleFds[0],dummy,sizeof(dummy)) >0);//将管道数组的数据消耗掉
                continue;
            }

            //处理普通的IO事件
            FdContext *fd_ctx=(FdContext*)event.data.ptr;//取出关联fd的上下文
            std::lock_guard<std::mutex> lock(fd_ctx->mutex);

            //处理当前异常事件(错误或挂起)，转换为读写事件
            //注意这里因为文件已经是错误的，所以我们所做的并非是在执行，而是触发事件后赶紧让错误文件清理滚蛋
            if(event.events &(EPOLLERR | EPOLLHUP))
            {
                //按照文件期望监控事件包含的读写事件添加到event.events中
                event.events|= (EPOLLIN | EPOLLOUT) & fd_ctx->events;
            }

            //解析就绪的真正事件
            int real_events =NONE;
            if(event.events & EPOLLIN)
            {
                real_events|=READ;
            }

            if(event.events & EPOLLOUT)
            {
                real_events|=WRITE;
            }

            //无事件，或者说当前要做的事件没有一个是文件期望要做的事件
            if((fd_ctx->events & real_events)==NONE)
            {
                continue;
            }


            //计算剩余未触发的事件
            int left_events =(fd_ctx->events & ~real_events);

            int op=left_events?EPOLL_CTL_MOD:EPOLL_CTL_DEL;//确定操作是修改还是删除
            event.events =EPOLLET |left_events;//边缘触发

            int rt2 =epoll_ctl(m_epfd,op,fd_ctx->fd,&event);//修改event
            if(rt2)
            {
                std::cerr<< "空闲函数epoll_ctl失败" << strerror(errno) <<std::endl;
                continue;//错了就下一个
            }

            //处理对应事件
            if(real_events & READ)
            {
                fd_ctx->triggerEvent(READ);//注意这里的触发事件并不等于执行回调函数，只是将对应回调函数加入任务队列
                --m_pendingEventCount;
            }

            if(real_events & WRITE)
            {
                fd_ctx ->triggerEvent(WRITE);
                --m_pendingEventCount;
            }

            
        }//for循环结束
        
        //交出CPU执行权，切换到其他协程
        //实际上这里是我们原本线程调度器的升级版，看到这里应该理解了idle在做什么
        Fiber::GetThis() ->yield();

    }//while循环结束


  

}



//当定时器添加到堆顶时调用该函数
void IOManager::onTimerInsertedFront()
{
        //唤醒空闲协程，重新计算超时时间
        tickle();
}


}