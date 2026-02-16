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

    //调用fstat，用于获取文件描述符m_fd相关的文件状态信息并存放在statbuf中,-1为无效
    if(fstat(m_fd,&statbuf)==-1)
    {
        m_isInit =false;
        m_isSocket =false;
    }
    else
    {
        m_isInit =true;
        m_isSocket=S_ISSOCK(statbuf.st_mode);//判断fd是否是套接字，使用st_mode中的位进行判断
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