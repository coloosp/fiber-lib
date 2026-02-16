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