#ifndef __SYLAR_TIMER_H__
#define __SYLAR_TIMER_H__

#include<memory>
#include<vector>
#include<set>
#include<shared_mutex>
#include<assert.h>
#include<functional>
#include<mutex>

namespace sylar{
//定时器管理类
class TimerManager;//先声明TimerManager类

//定时器类
//这个继承模板我们之前见过，作用是同步外部和内部的shared_ptr
class Timer : public std::enable_shared_from_this<Timer>
{

friend class TimerManager;//友元，可以允许TimerManager访问Timer的私有成员

public:
//从时间堆里面删除Timer定时器
bool cancel();
//刷新定时器的超时时间
bool refresh();
//重设定时器的超时时间
//传入新的超时时间，精确到毫秒，是否从当前时间替换
bool reset(uint64_t ms,bool from_now);

private:
//私有构造函数
//传入超时时间，超时触发的回调函数，是否触发循环，定时器管理器指针
Timer(uint64_t ms,std::function<void()> cb,bool recurring,TimerManager* manager);


private:
//是否触发循环
bool m_recurring =false;
//超时时间
uint64_t m_ms=0;
//绝对超时时间，定时器下一次触发的时间点
std::chrono::time_point<std::chrono::system_clock> m_next;
//超时触发的回调函数
std::function<void()> m_cb;
//该定时器所属定时管理器指针
TimerManager* m_manager =nullptr;

private:
//为最小堆实现的重载运算符
struct Comparator
{
    //函数声明
    //比较两操作数,在后面会比较两计时器的
bool operator()(const std::shared_ptr<Timer> &lhs,const std::shared_ptr<Timer> &rhs) const;
};

};

class TimerManager
{
friend class Timer;//声明定时器类为友元类

public:
//构造函数
TimerManager();
//虚析构函数
virtual ~TimerManager();

//创建定时器，这里实际上传入了管理器的指针this到定时器的构造函数中，这是工厂模式的特性
std::shared_ptr<Timer> addTimer(uint64_t ms,std::function<void()> cb,bool recurring =false);

//虽然weak_ptr在其他地方的作用是解决循环引用，这里实际上是避免回调函数访问对象已经被析构
//weak_ptr实际上是回调函数的参数
std::shared_ptr<Timer> addConditionTimer(uint64_t ms,std::function <void()> cb,std::weak_ptr<void> weak_cond,bool recurring=false);

//获取时间堆最近的一个定时器的超时时间
uint64_t getNextTimer();

//参数是回调函数的vector数组
//取出所有超时定时器的回调函数传入cbs中
void listExpiredCb(std::vector<std::function<void()>> &cbs);

//时间堆中是否存在有效定时器
bool hasTimer();

protected:
//当最早超时的定时器添加到时间堆堆顶时调用该函数
virtual void onTimerInsertedFront() {};

//添加定时器到时间堆，这里属于重载函数
void addTimer(std::shared_ptr<Timer> timer);

private:
//检测系统时钟是否回退
bool detectClockRollover();

private:
std::shared_mutex m_mutex;//共享读写锁
//使用set来模拟时间堆
std::set<std::shared_ptr<Timer>,Timer::Comparator> m_timers;
//在下一次getNextTimer()执行前，标记onTimerInsertedFront是否执行过
bool m_tickled=false;
//上次检查系统时间是否回退的绝对时间
std::chrono::time_point<std::chrono::system_clock> m_previouseTime;
};





}
#endif 