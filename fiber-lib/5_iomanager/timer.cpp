# include "timer.h"

namespace sylar{

    //取消定时器
bool Timer::cancel()
{
    //独占写入锁
std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

//回调函数为空说明定时器已经失效
if(m_cb==nullptr)
{
    return false;
}
else{
    m_cb=nullptr;//置空回调函数
}

//在时间堆中获取对应定时器的迭代器
auto it=m_manager->m_timers.find(shared_from_this());


if(it!=m_manager->m_timers.end()){//找到时
    m_manager->m_timers.erase(it);//移除对应定时器
}
//实际上没有找到也会返回true,只要不是重复删除就行
return true;

}

//刷新定时器
bool Timer::refresh()
{

    std::unique_lock<std::shared_mutex> write_lock(m_manager->m_mutex);

    if(m_cb==nullptr)//定时器已经死掉了
    {
        return false;
    }


    auto it=m_manager->m_timers.find(shared_from_this());
    if(it==m_manager->m_timers.end())//没找到
    {
        return false;
    }

    m_manager->m_timers.erase(it);//先需要删除旧的定时器
    //新的绝对超时时间为当前系统时间+原本设定的超时时间
    m_next= std::chrono::system_clock::now() +std::chrono::milliseconds(m_ms);

    m_manager->m_timers.insert(shared_from_this());//重新加入

    return true;

}

//重设定时器，传入新的超时毫秒数以及是否从当前时间计算
bool Timer::reset(uint64_t ms,bool from_now)
{
    //重设时间和原时间相同且不需要从当前时间开始计算
    if(ms==m_ms && ! from_now)
    {
        return true;
    }


    { //用于加锁的作用域
        std::unique_lock<std::shared_mutex> write(m_manager->m_mutex);

        if(!m_cb)
        {
            return false;
        }

        auto it =m_manager->m_timers.find(shared_from_this());

        if(it==m_manager->m_timers.end()){
            return false;
        }
        m_manager->m_timers.erase(it);

    }

    //如果为真，起始时间会被设置为当前系统时间，若为假，起始时间应该为原开始时间(原绝对超时时间-原超时时间)
    auto start=from_now? std::chrono::system_clock::now() : m_next-std::chrono::milliseconds(m_ms);

    m_ms=ms;//更新超时时间

    m_next =start+std::chrono::milliseconds(m_ms);//更新绝对超时时间
    m_manager->addTimer(shared_from_this());
    return true;
}

//定时器构造函数
Timer::Timer(uint64_t ms,std::function<void()> cb,bool recurring,TimerManager* manager):
m_recurring(recurring),m_ms(ms),m_manager(manager),m_cb(cb)
{
    auto now =std::chrono::system_clock::now();//记录当前系统时间

    m_next=now+std::chrono::milliseconds(m_ms);

}

//重载运算符实现
bool Timer::Comparator::operator()(const std::shared_ptr<Timer> &lhs,const std::shared_ptr<Timer> &rhs) const
{
    assert(lhs!=nullptr && rhs!=nullptr);//保证定时器存在

    return lhs->m_next < rhs->m_next;//按照升序排列
}

//定时器管理类构造函数
TimerManager::TimerManager()
{
    //为检测时间是否回退detectClockRollover提供基准时间
    m_previouseTime =std::chrono::system_clock::now();
}

//析构函数空实现，这是因为时间堆存储的是智能指针的好处
TimerManager::~TimerManager()
{

}

//创建普通计时器
std::shared_ptr<Timer> TimerManager::addTimer(uint64_t ms,std::function<void()> cb,bool recurring)
{
    //创建计时器，返回智能指针
    std::shared_ptr<Timer> timer (new Timer(ms,cb,recurring,this));

    addTimer(timer);//虽然同名，但这里是重载函数，作用就是将定时器插入时间堆

    return timer;
}

//检验weak_ptr指向的需要检查的业务对象是否存活,这里的cb和定时器的m_cb并不是一个东西而是提前传入的原始回调函数
static void OnTimer(std::weak_ptr<void> weak_cond,std::function<void()> cb)
{
    //weak_ptr需要转化为shared_ptr才能正常使用   
    std::shared_ptr<void> tmp =weak_cond.lock();
    if(tmp)
    {
        cb();
    }

}

//创建附带条件的定时器，这里的weak_ptr是回调函数的参数，它的作用是指向需要检查是否存货的业务对象，类型是万能类型
std::shared_ptr<Timer> TimerManager::addConditionTimer(uint64_t ms,std::function<void()> cb,std::weak_ptr<void> weak_cond,bool recurring)
{

    //bind会将OnTimer封装成新的回调函数,weak_cond和cb是它的参数，再创建定时器
    return addTimer(ms,std::bind(&OnTimer,weak_cond,cb),recurring);

}

//获得时间堆中堆顶的时间计时器的剩余超时时间
uint64_t TimerManager::getNextTimer()
{
    //共享读锁
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    m_tickled=false;//重置m_tickled标记

    if(m_timers.empty())
    {
        return ~0ull;//表示不存在定时器
    }

    auto now=std::chrono::system_clock::now();//当前系统时间
    auto time=(*m_timers.begin())->m_next;//获取时间堆堆顶的绝对超时时间,这里的解引用结果是定时器的智能指针

    if(now >=time)
    {
        return 0;//已经超时
    }
    else{
        //获取剩余时间的间隔
        auto duration =std::chrono::duration_cast<std::chrono::milliseconds>(time-now);
        return static_cast<uint64_t>(duration.count());//将间隔转换为秒数返回
    }


}

//获取时间堆中所有已经超时的定时器，并且存入cbs数组中
void TimerManager::listExpiredCb(std::vector<std::function<void()>> &cbs)
{

    //写共享锁
    std::unique_lock<std::shared_mutex> write_lock(m_mutex);

    auto now =std::chrono::system_clock::now();

    //检查时钟是否回滚
    bool rollover =detectClockRollover();

    //时间堆不为空，保证时钟没有回滚且堆顶定时器已超时
    //迭代器要解引用后才能获取智能指针
    while(!m_timers.empty() && (rollover || (*m_timers.begin())->m_next<=now))
    {

        std::shared_ptr<Timer> temp= *m_timers.begin();//获取堆顶定时器

        m_timers.erase(m_timers.begin());

        cbs.push_back(temp->m_cb);//加入堆顶定时器回调函数

        //处理循环定时器
        if(temp->m_recurring)
        {
            temp->m_next=now+std::chrono::milliseconds(temp->m_ms);

            m_timers.insert(temp);
        }
        else
        {
            temp->m_cb=nullptr;//非循环定时器清空回调函数
        }
    }
}

//判断时间堆是否为空
bool TimerManager::hasTimer()
{
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);
    return !m_timers.empty();
}

//添加定时器到时间堆
void TimerManager::addTimer(std::shared_ptr<Timer> timer)
{
    bool at_front =false;//标记添加的定时器是否成为堆顶

    {
        std::unique_lock<std::shared_mutex> write_lock(m_mutex);//共享写锁
        auto it =m_timers.insert(timer).first;
        //判断新插入的定时器是否在定时器堆顶却未触发过onTimerInsertedFront()
        at_front=(it==m_timers.begin()) && !m_tickled;

        if(at_front)
        {
            m_tickled=true;
        }

    }
    if(at_front)
    {
        onTimerInsertedFront();//唤醒
    }

}

//检测系统时钟是否回退
bool TimerManager::detectClockRollover()
{
    bool rollover =false;
    auto now =std::chrono::system_clock::now();
    //如果当前时间比上次检查时间少一小时以上说明时钟回退
    if(now <(m_previouseTime- std::chrono::milliseconds(60*60*1000)))
    {
        rollover=true;
    }
    m_previouseTime =now;//更新基准时间
    return rollover;
}



}