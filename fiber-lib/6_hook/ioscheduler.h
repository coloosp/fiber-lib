#ifndef _SYLAR_IOMANAGER_H_
#define _SYLAR_IOMANAGER_H_

#include "scheduler.h"
#include "timer.h"

namespace sylar{

class IOManager :public Scheduler,public TimerManager
{
public :
    //枚举宏定义事件，后续通过与或运算来更新与判断包含事件
    enum Event
    {
        NONE =0x0, //无事件
        READ =0x1, //读事件
        WRITE=0X4  //写事件
    };
private:
    //用于描述一个文件的上下文结构
    struct FdContext
    {
        //描述一个事件的上下文结构
        struct EventContext
        {
            Scheduler *scheduler =nullptr; //关联的调度器
            std::shared_ptr<Fiber> fiber;  //回调协程
            std::function<void()> cb; //回调函数
        };

        EventContext read;//读事件上下文
        EventContext write;//写事件上下文
        int fd=0;//事件关联的文件描述符(句柄)
        Event events =NONE;//当前注册事件，这是用户期望监控事件
        std::mutex mutex;

        EventContext& getEventContext(Event event);//获取事件上下文,返回值为引用
        void resetEventContext(EventContext &ctx); //重置事件上下文
        void triggerEvent(Event event); //触发对应事件

    };

public:

    //构造函数，传入工作线程数，是否把主线程当做工作线程，IOManger的名称
    IOManager(size_t threads =1,bool use_caller =true,const std::string &name="IOManager");
    ~IOManager();

    //给指定fd添加IO事件
    int addEvent(int fd,Event event,std::function<void()> cb=nullptr);

    //删除指定fd的指定事件，只注销不触发回调
    bool delEvent(int fd,Event event);

    //取消指定fd的指定事件，注销并触发回调
    bool cancelEvent(int fd,Event event);

    //取消fd中所有事件,触发全部事件的回调
    bool cancelAll(int fd);

    static IOManager* GetThis();//获取当前线程的IOManager实例指针
    

protected:
//还记得我们在scheduler未实现的唤醒函数吗
//重写scheduler类中的唤醒线程函数
void tickle() override;

//重写scheduler类中的停止判断函数
bool stopping() override; 

//重写scheduler类中的空闲线程工作函数
void idle() override;

//重写TimerManager的onTimerInsertedFront函数
//当定时器添加到时间堆顶时调用
void onTimerInsertedFront() override;

//调整fd上下文数组大小
void contextResize(size_t size);

private:
int m_epfd =0;//epoll的文件描述符(句柄)
int m_tickleFds[2];//用于线程间通信的管道描述符 0表示读 1表示写
std::atomic<size_t> m_pendingEventCount; //待处理的IO事件数
std::shared_mutex m_mutex;//读写锁
std::vector<FdContext *> m_fdContexts;//存储所有文件上下文结构数组，下标即为文件描述符


};


}

#endif