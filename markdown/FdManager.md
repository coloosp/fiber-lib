## Fd_manager
定义了FdCtx和FdManager两个类
    FdCtx记录了关于对于文件的读写超时时间，阻塞信息，以及判定是否关闭和是否是Socket
    Fdmanager类则负责管理FdCtx的创建与获取，并且存放所有FdCtx

注意，虽然在iomanager模块也有文件上下文，但请务必区分，共同点是它们都是对文件的描述，但实际上描述的信息有所不同的，具体如何区分请看清楚是使用什么管理器进行调度的

fd_manager是对于后续hook的补充类，所以文件夹是和hook放在一起的

## fd_manager.h
### 代码
```
#ifndef _FD_MANAGER_H_
#define _FD_MANAGER_H_

#include <memory>
#include <shared_mutex>
#include"thread.h"

namespace sylar{

    //文件上下文类
class FdCtx : public std::enable_shared_from_this<FdCtx>
{
private:
    bool m_isInit =false; //fd上下文是否初始化完成
    bool m_isSocket =false;//fd是否为套接字
    bool m_sysNonblock =false;//内核层面是否设置为非阻塞
    bool m_userNonblock =false;//用户是否要求非阻塞
    bool m_isClosed =false; //fd是否已经关闭
    int m_fd;

    uint64_t m_recvTimeout =(uint64_t)-1;//读事件超时时间
    uint64_t m_sendTimeout =(uint64_t)-1;//写事件超时时间


public:
    FdCtx(int fd);//构造函数
    ~FdCtx();

    bool init();
    bool isInit() const {return m_isInit;} //判断初始化
    bool isSocket() const {return m_isSocket;} //判断socket
    bool isClosed() const {return m_isClosed;} //判断是否关闭

    void setUserNonblock(bool v) {m_userNonblock =v;} //设置用户非阻塞标记
    bool getUserNonblock() const {return m_userNonblock;} //获取用户层非阻塞标记

    void setSysNonblock(bool v) {m_sysNonblock=v;} //设置内核层非阻塞标记
    bool getSysNonblock() const {return m_sysNonblock;} //获取内核层非阻塞状态

    void setTimeout(int type,uint64_t v);//设置超时时间
    uint64_t getTimeout(int type);

};

//Fd上下文管理器
class FdManager
{
public:
    FdManager();

    //获取指定文件上下文
    //auto_create为不存在时自动创建FdCtx
    std::shared_ptr<FdCtx> get(int fd,bool auto_create =false);

    void del(int fd);
private:
    std::shared_mutex m_mutex;
    //文件上下文数组，使用fd直接索引
    std::vector<std::shared_ptr<FdCtx>> m_datas;
};

//模板 单例类,保证FdManager只有一个实例存在
template <typename T>
class Singleton
{
private:
static T* instance;//实例指针
static std::mutex mutex;

protected:
    Singleton() {};//构造函数

public:
    //禁止调用拷贝构造和赋值运算符
    Singleton(const Singleton&) =delete;
    Singleton& operator =(const Singleton&) =delete;

    //获取唯一实例指针
    static T* GetInstance()
    {
        std::lock_guard<std::mutex> lock(mutex);
        //首次调用时才会创建实例,懒汉式设计
        if(instance ==nullptr)
        {
            instance =new T();
        }
        return instance;
    }

    //销毁单例实例
    static void DestroyInstance()
    {
        std::lock_guard<std::mutex> lock(mutex);
        delete instance; //释放实例内存
        instance =nullptr; //重置指针
    }

};

//单例调用FdManager
typedef Singleton<FdManager> FdMgr;

}

#endif
```
### 解析
* 单例模式
单例模式是在整个程序生命周期内，一个类构造的实例只能存在一个，保证了唯一性。对于本模块，就是保证了FdManager的唯一性。
采用了懒汉式，当实例不存在时，当需要使用该实例会自动创建并使用该实例，我们在获取实例这里实现
```
    static T* GetInstance()
    {
        std::lock_guard<std::mutex> lock(mutex);
        //首次调用时才会创建实例,懒汉式设计
        if(instance ==nullptr)
        {
            instance =new T();
        }
        return instance;
    }
```
实际上这种思想我们已经见过了，获取主协程的`std::shared_ptr<Fiber> Fiber::GetThis()`我们就使用了接近懒汉式的设计方式
更多详情可以了解：
[单例模式总结](https://blog.csdn.net/unonoi/article/details/121138176)
## fd_manager.cpp
### 代码
```
#include "fd_manager.h"
#include "hook.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

namespace sylar{

//显式实例化FdManager单例模板类
template class Singleton<FdManager>;

template<typename T>
T* Singleton<T>::instance =nullptr;//初始化指针

//初始化单例互斥锁
template<typename T>
std::mutex Singleton<T>::mutex;

//构造函数
FdCtx::FdCtx(int fd):
m_fd(fd)
{
    init();//初始化
}

FdCtx::~FdCtx()
{

}

//初始化
bool FdCtx::init()
{
    if(m_isInit) //已经初始化了
    {
        return  true;
    }

    struct stat statbuf;//存储fd的文件属性

    //调用fstat获取fd属性,-1为无效
    if(fstat(m_fd,&statbuf)==-1)
    {
        m_isInit =false;
        m_isSocket =false;
    }
    else
    {
        m_isInit =true;
        m_isSocket=S_ISSOCK(statbuf.st_mode);//判断fd是否是套接字
    }

    //如果是套接字
    if(m_isSocket)
    {
        //fcntl_f是被hook前的fcntl函数,F_GETFL是获取当前文件状态
        //获取的flags是一个状态集合
        int flags =fcntl_f(m_fd,F_GETFL,0);

        //判断当前是否是非阻塞模式，别忘了我们&运算的知识
        if(!(flags & O_NONBLOCK))
        {
            fcntl_f(m_fd,F_SETFL,flags|O_NONBLOCK);//修改fd状态为非阻塞模式
        }
        m_sysNonblock =true;
    }
    else
    {
        m_sysNonblock =false;//非套接字不设置
    }

    return m_isInit;


}

//设置超时时间，传入参数为超时类型和超时时间
void FdCtx::setTimeout(int type,uint64_t v)
{
    if(type==SO_RCVTIMEO) //系统定义的读超时宏
    {
        m_recvTimeout =v; //设置读事件超时时间
    }
    else//否则是写超时
    {
        m_sendTimeout =v;
    }
}

//获取超时时间
uint64_t FdCtx::getTimeout(int type)
{
    if(type==SO_RCVTIMEO)
    {
        return m_recvTimeout;
    }
    else{
        return m_sendTimeout;
    }
}


//构造函数
FdManager::FdManager()
{
    m_datas.resize(64);//初始化文件上下文数组
}

//获取文件上下文
std::shared_ptr<FdCtx> FdManager::get(int fd,bool auto_create)
{
    if(fd==-1)//无效文件
    {
        return nullptr;
    }

    //共享读锁，注意在读锁时不要写入和创建
    std::shared_lock<std::shared_mutex> read_lock(m_mutex);

    if(m_datas.size() <=fd) //不在文件上下文数组内
    {
        if(auto_create ==false) //不自动创建
        {
            return nullptr;
        }
    }
    else
    {
        //fd对应上下文存在或者不自动创建时
        if(m_datas[fd]|| !auto_create)
        {
            return m_datas[fd];
        }
    }

    //接下来是需要创建上下文了
    read_lock.unlock();
    std::unique_lock<std::shared_mutex> write_lock(m_mutex);

    //需要扩容时
    if(m_datas.size() <=fd)
    {
        m_datas.resize(fd*1.5);
    }

    m_datas[fd]=std::make_shared<FdCtx>(fd);//创建新的FdCtx对象

    return m_datas[fd];

}

void FdManager::del(int fd)
{
    //共享读锁
    std::unique_lock<std::shared_mutex> write_lock(m_mutex);

    if(m_datas.size() <=fd)//fd超过范围
    {
        return ;
    }

    m_datas[fd].reset();
}




}
```

应该没有过分难以理解的，说说为什么要判断套接字与为什么要判断阻塞信息，这部分其实放在hook说也行
这是因为只有套接字文件（可能还包含我们之前在iomanager的管道数组）会发生IO变化，只有它们是会产生数据输入的情况，我们就需要使用epoll来监控它们的数据就绪情况，在数据没有就绪时挂起，在数据就绪时触发事件并加入任务队列。
至于其他普通文件，它们是死的，处理它们不用担心等待的问题，现拿现用即可。
至于阻塞信息判断，这是因为只有在处于非阻塞模式下，我们才对例如读事件或者写事件进行挂起等操作。反过来，如果是阻塞模式，我们就不会挂起，会一直等待对方数据就绪直到天荒地老。