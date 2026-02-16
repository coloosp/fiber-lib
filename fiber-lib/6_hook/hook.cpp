#include "hook.h"
#include "ioscheduler.h"
#include <dlfcn.h>
#include <cstdarg>
#include "fd_manager.h"
#include <string.h>

//宏定义，XX为占位符
#define HOOK_FUN(XX)\
        XX(sleep)\
        XX(usleep)\
        XX(nanosleep)\
        XX(socket)\
        XX(connect)\
        XX(accept)\
        XX(read)\
        XX(readv)\
        XX(recv)\
        XX(recvfrom)\
        XX(recvmsg)\
        XX(write)\
        XX(writev)\
        XX(send)\
        XX(sendto)\
        XX(sendmsg)\
        XX(close)\
        XX(fcntl)\
        XX(ioctl)\
        XX(getsockopt)\
        XX(setsockopt)

namespace sylar
{

    static thread_local bool t_hook_enable =false;//线程局部变量，hook开启标记

    //获取hook开启状态
    bool is_hook_enable()
    {
        return t_hook_enable;
    }

    //设置hook开关
    void set_hook_enable(bool flag)
    {
        t_hook_enable =flag;
    }

    void hook_init()
    {
        static bool is_inited=false;//标记是否初始化，静态变量
        if(is_inited)
        {
            return ;
        }
    
    is_inited =true;

    //宏替换函数 dlsym的作用是查找系统原始函数
    //比较复杂可以到文档查看
    #define XX(name) name ## _f =(name ##_fun)dlsym(RTLD_NEXT,#name);
        HOOK_FUN(XX) //批量为之前定义的函数指针赋值
    #undef XX //释放宏定义


    }

    //全局初始化结构体
    struct HookIniter
    {
        HookIniter()
            {
                hook_init();
            }
    };

static HookIniter s_hook_initer;//静态初始化结构体实例


} //sylar

//定时器信息结构体
struct timer_info
{
    int cancelled =0;
};

//通用IO操作函数，使用模板，封装了读/写类型的逻辑
//args使用了完美转发，可以保留左值和右值特性,表示我们的其余参数
//OriginFun为原始函数类型，event为IO事件类型
template <typename OriginFun,typename... Args>
static ssize_t do_io(int fd,OriginFun fun,const char* hook_fun_name,uint32_t event,int timeout_so,Args&&... args)
{
    //需要开启hook
    if(!sylar::t_hook_enable)
    {
        return fun(fd,std::forward<Args>(args)...);
    }

    //获取fd上下文, FdMgr是FdManager的单例模式实例，我们在前面typedef了
    std::shared_ptr<sylar::FdCtx> ctx =sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx)
    {
        return fun(fd,std::forward<Args>(args)...);
    }

    if(ctx->isClosed())
    {
        errno =EBADF;//EBADF表示无效的文件描述符
        return -1;
    }

    //非socket或者设置了非阻塞
    if(!ctx->isSocket() || ctx ->getUserNonblock())
    {
        return fun(fd,std::forward<Args>(args)...);
    }

    //获取fd超时时间
    uint64_t timeout =ctx->getTimeout(timeout_so);

    //构造定时器信息类实例
    std::shared_ptr<timer_info> tinfo(new timer_info);

//重试标签，当IO未就绪时，挂起协程重试，配合后面的goto
retry:

    //执行IO操作
    ssize_t n=fun(fd,std::forward<Args>(args)...);

    //处理信号中断错误，中断就重试
    while(n==-1 && errno==EINTR )
    {
        n=fun(fd,std::forward<Args>(args)...);
    }

    //EAGAIN表示资源暂时不可用,需要挂起
    if(n==-1 &&errno ==EAGAIN)
    {
        //获取IO调度器实例
        sylar::IOManager* iom=sylar::IOManager::GetThis();
        //构造定时器
        std::shared_ptr<sylar::Timer> timer;
        //这里的weak_ptr就是为了避免tinfo循环引用以及确认对象参数是否有效
        std::weak_ptr<timer_info> winfo(tinfo);

        //设置超时时间，当timeout为(uint64_t)-1时表示永久等待
        if(timeout !=(uint64_t)-1)
        {
            //可以回忆一下我们在timer的条件定时器函数，这里使用了lambda表达式[]里面的是捕获参数
            //请看清楚我们的函数在哪里结束
            timer =iom ->addConditionTimer(timeout,[winfo,fd,iom,event]()//这里传入了timeout,与回调函数
            //以下是lambda表达式内容
            {
                //检查tinfo是否有效
                auto t=winfo.lock();
                if(!t || t->cancelled)
                {
                    return ;
                }
                t->cancelled =ETIMEDOUT;//标记为超时

                iom->cancelEvent(fd,(sylar::IOManager::Event)(event));
            },winfo);//这里传入第三个参数，函数结束

        }   //if(timeout !=(uint64_t)-1)

        //各位可以再回去看看我们的addEvent的运行逻辑，如果这里不传入回调函数，会进入fiber判断，执行当前协程
        //添加IO事件
        int rt =iom->addEvent(fd,(sylar::IOManager::Event)(event));
        //添加事件失败，rt为-1
        if(rt)
        {
            std::cout <<hook_fun_name <<"addEvent(" <<fd <<","<< event <<")" <<std::endl;
            if(timer)
            {
                timer->cancel();//取消该定时器
            }
            return -1;
         }
         else //事件添加成功，挂起当前协程,讲CPU执行权交给其他协程，一边凉快去
         {
            sylar::Fiber::GetThis()->yield();
            
            //这里是再次执行时从时间堆里面取出计时器
            //再次执行有两种情况，获得了资源，或者计时器超时
            if(timer)
            {
                timer->cancel();
            }

            //检查是否超时
            if(tinfo->cancelled ==ETIMEDOUT)
            {
                errno =tinfo->cancelled; //获取错误码
                return -1;
            }
            goto retry;//跳转到retry标签
         }
    }  //if(n==-1 &&errno ==EAGAIN)

    return n; //到这里说明处理成功了

}


extern "C"{

    //宏定义
#define XX(name) name ## _fun name ## _f=nullptr;
    HOOK_FUN(XX)
#undef XX

//睡眠函数
unsigned int sleep(unsigned int seconds)
{
    //未开启hook使用原始sleep
    if(!sylar::t_hook_enable)
    {
        return sleep_f(seconds);
    }
    //获得当前协程对象
    std::shared_ptr <sylar::Fiber> fiber=sylar::Fiber::GetThis();
    sylar::IOManager* iom=sylar::IOManager::GetThis();

    //将睡眠函数替换为定时器，等待secons秒后将协程再加入任务队列
    iom->addTimer(seconds*1000,[fiber,iom](){iom->scheduleLock(fiber,-1);});

    fiber->yield();//挂起了就别在占着线程！
    return 0;
}

//和前面一样，这里是微秒级睡眠
int usleep(useconds_t usec)
{
    if(!sylar::t_hook_enable)
    {
        return usleep_f(usec);
    }

    std::shared_ptr<sylar::Fiber> fiber =sylar::Fiber::GetThis();
    sylar::IOManager* iom=sylar::IOManager::GetThis();
    iom->addTimer(usec/1000,[fiber,iom](){iom->scheduleLock(fiber,-1);});
    fiber->yield();
    return 0;
}

//纳秒级休眠
int nanosleep(const struct timespec* req,struct timespec* rem)
{
    if(!sylar::t_hook_enable)
    {
        return nanosleep_f(req,rem);
    }

    //超时时间等于秒转毫秒+纳秒转毫秒
    int timeout_ms =req->tv_sec*1000+req->tv_nsec/1000/1000;

    std::shared_ptr<sylar::Fiber> fiber =sylar::Fiber::GetThis();
    sylar::IOManager* iom=sylar::IOManager::GetThis();
    iom->addTimer(timeout_ms,[fiber,iom](){iom->scheduleLock(fiber,-1);});
    fiber->yield();
    return 0;
}

//socket函数，创建sock并初始化上下文
int socket(int domain,int type,int protocol)
{
    if(!sylar::t_hook_enable)
    {
        return socket_f(domain,type,protocol);
    }

    int fd=socket_f(domain,type,protocol);
    if(fd==-1)
    {
        std::cerr <<"socket()失败:" <<strerror(errno) <<std::endl;
        return fd; 
    }

    //auto_create为true时自动创建fd上下文
    sylar::FdMgr::GetInstance()->get(fd,true);
    return fd;

}

//
int connect_with_timeout(int fd,const struct sockaddr* addr,socklen_t addrlen,uint64_t timeout_ms)
{
    if(!sylar::t_hook_enable)
    {
        return connect_f(fd,addr,addrlen);
    }

    //获取fd上下文，这里就不能自动创建了
    std::shared_ptr<sylar::FdCtx> ctx =sylar::FdMgr::GetInstance()->get(fd);
    if(!ctx ||ctx ->isClosed())
    {
        errno=EBADF;//fd上下文无效
        return -1;
    }

    //不是socket按照原始函数处理
    if(!ctx->isSocket())
    {
        return connect_f(fd,addr,addrlen);
    }
    //非阻塞模式按照原始函数处理
    if(ctx->getUserNonblock())
    {
        return connect_f(fd,addr,addrlen);
    }

    int n=connect_f(fd,addr,addrlen);
    //EINPROGRESS表示连接中
    if(n==0) //连接成功
    {
        return 0;
    }
    else if(n!=-1 || errno!=EINPROGRESS) //连接失败
    {
        return  n;
    }

    //剩下的情况是连接中
    //你知道的，我们肯定不会在这里傻等，所以设置事件+添加超时定时器
    sylar::IOManager* iom=sylar::IOManager::GetThis();
    std::shared_ptr<sylar::Timer> timer;
    std::shared_ptr<timer_info> tinfo(new timer_info);
    std::weak_ptr<timer_info> winfo(tinfo);

    //和do_io一样的逻辑
    if(timeout_ms !=(uint64_t)-1)
    {
        timer =iom->addConditionTimer(timeout_ms,[winfo,fd,iom]()
        {
            auto t=winfo.lock();
            if(!t || t->cancelled)
            {
                return;
            }
            t->cancelled =ETIMEDOUT;//标记超时
            iom->cancelEvent(fd,sylar::IOManager::WRITE);//别忘记这里是取消事件并触发回调
        },winfo);
    }
    int rt=iom ->addEvent(fd,sylar::IOManager::WRITE);//添加写事件
    if(rt==0)
    {
        sylar::Fiber::GetThis()->yield();//挂起协程

        //恢复协程后取消定时器
        if(timer)
        {
            timer->cancel();
        }

        //说明是超时触发的协程恢复
        if(tinfo->cancelled)
        {
            errno=tinfo->cancelled;
            return -1;
        }
    }
    else    //添加失败
    {
        if(timer)
        {
            timer->cancel();
        }
        std::cerr <<"connect addEvent" <<fd <<",WRITE 事件错误";
    }
    //检查连接结果
    int error =0;
    socklen_t len=sizeof(int);
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)==-1)
    {
        return -1;
    }
    if(!error) //连接成功
    {
        return 0;
    }
    else
    {
        errno =error;
        return -1;
    }
}

//connect超时时间
static uint64_t s_connect_timeout =1;

//connect函数调用connect_with_timeout函数
int connect(int sockfd,const struct sockaddr*addr,socklen_t addrlen)
{
    return connect_with_timeout(sockfd,addr,addrlen,s_connect_timeout);
}


int accept(int sockfd,struct sockaddr *addr,socklen_t *addrlen)
{
    //通用IO模板，等待READ事件
    int fd=do_io(sockfd,accept_f,"accept",sylar::IOManager::READ,SO_RCVTIMEO,addr,addrlen);

    //连接成功，初始化fd上下文
    if(fd >=0)
    {
        sylar::FdMgr::GetInstance()->get(fd,true);
    }
    return fd;
}

//一样的写法，都是劫持系统函数并调用通用模板
ssize_t read(int fd,void *buf,size_t count)
{
    return do_io(fd,read_f,"read",sylar::IOManager::READ,SO_RCVTIMEO,buf,count);
}

ssize_t readv(int fd,const struct iovec *iov,int iovcnt)
{
   return do_io(fd, readv_f, "readv", sylar::IOManager::READ, SO_RCVTIMEO, iov, iovcnt);
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags)
{
	return do_io(sockfd, recv_f, "recv", sylar::IOManager::READ, SO_RCVTIMEO, buf, len, flags);	
}

ssize_t recvfrom(int sockfd,void *buf,size_t len,int flags,struct sockaddr *src_addr,socklen_t *addrlen)
{
    return do_io(sockfd,recvfrom_f,"recvfrom",sylar::IOManager::READ,SO_RCVTIMEO,buf,len,flags,src_addr,addrlen);
}

ssize_t recvmsg(int sockfd,struct msghdr *msg,int flags)
{
    return do_io(sockfd,recvmsg_f,"recvmsg",sylar::IOManager::READ,SO_RCVTIMEO,msg,flags);
}

ssize_t write(int fd,const void *buf,size_t count)
{
    return do_io(fd,write_f,"write",sylar::IOManager::WRITE,SO_SNDTIMEO,buf,count);
}

ssize_t writev(int fd,const struct iovec *iov,int iovcnt)
{
    return do_io(fd,writev_f,"writev",sylar::IOManager::WRITE,SO_SNDTIMEO,iov,iovcnt);
}

ssize_t send(int sockfd,const void *buf,size_t len,int flags)
{
    return do_io(sockfd,send_f,"send",sylar::IOManager::WRITE,SO_SNDTIMEO,buf,len,flags);
}

ssize_t sendto(int sockfd,const void *buf,size_t len,int flags,const struct sockaddr *dest_addr, socklen_t addrlen)
{
    return do_io(sockfd,sendto_f,"sendto",sylar::IOManager::WRITE,SO_SNDTIMEO,buf,len,flags,dest_addr, addrlen);	
}

ssize_t sendmsg(int sockfd,const struct msghdr *msg,int flags)
{
    return do_io(sockfd,sendmsg_f,"sendmsg",sylar::IOManager::WRITE,SO_SNDTIMEO,msg,flags);
}

//关闭fd前，清理IO事件和上下文
int close(int fd)
{
    if(!sylar::t_hook_enable)
    {
        return close_f(fd);
    }

    //获取文件上下文
    std::shared_ptr<sylar::FdCtx> ctx=sylar::FdMgr::GetInstance()->get(fd);

    if(ctx)
    {
        auto iom=sylar::IOManager::GetThis();
        if(iom)
        {
            iom->cancelAll(fd);//取消并触发所有事件
        }
        sylar::FdMgr::GetInstance()->del(fd);
    }
    return close_f(fd);//再调用系统函数关闭fd
}

//文件控制函数
int fcntl(int fd,int cmd,...)
{
    va_list va;//可变参数列表

    va_start(va,cmd);//初始化可变参数列表，接受cmd后的参数
    switch(cmd)
    {
        case F_SETFL: //设置文件状态标志
        {
            int arg= va_arg(va,int);//获取int型可变参数
            va_end(va);

            std::shared_ptr<sylar::FdCtx> ctx=sylar::FdMgr::GetInstance()->get(fd);
            //不存在/已关闭/非套接字
            if(!ctx ||ctx->isClosed() || !ctx->isSocket())
            {
                return fcntl_f(fd,cmd,arg);
            }

            ctx->setUserNonblock(arg & O_NONBLOCK);//记录非阻塞标记,别忘了位运算
            if(ctx->getSysNonblock()) //用户说什么就是什么，系统不对就改系统的
            {
                arg|=O_NONBLOCK;//添加
            }
            else
            {
                arg &=~O_NONBLOCK;//移除
            }
            return fcntl_f(fd,cmd,arg);
        }
        break;
        //获取文件状态标志，注意区分系统和用户
        case F_GETFL:
        {
            va_end(va);
            int arg=fcntl_f(fd,cmd);//调用原始函数获取系统层标志
            std::shared_ptr<sylar::FdCtx> ctx=sylar::FdMgr::GetInstance()->get(fd);

            if(!ctx ||ctx->isClosed() || !ctx->isSocket())
            {
                return arg;
            }

            if(ctx->getUserNonblock())
            {
                return arg |O_NONBLOCK;//添加
            }
            else
            {
                return arg &~ O_NONBLOCK;//移除
            }
        }
        break;

        //下面这些是无其他参数的cmd
        case F_GETFD:
        case F_GETOWN:
        case F_GETLEASE:
#ifdef F_GETPIPE_SZ //这是跨平台兼容
        case F_GETPIPE_SZ:
#endif
        {
            va_end(va);//清理va
            return fcntl_f(fd,cmd);
        }
        break;

        //处理结构体参数
        case F_GETOWN_EX:
        case F_SETOWN_EX:
        {
            struct f_owner_exlock* arg =va_arg(va,struct f_owner_exlock*);
            va_end(va);
            return fcntl_f(fd,cmd,arg);
        }
        break;

        default://其他情况直接调用原始函数即可
            va_end(va);
            return fcntl_f(fd,cmd);
    }
}

//设备控制接口函数
int ioctl(int fd,unsigned long request,...)
{
    va_list va;
    va_start(va,request);
    void* arg=va_arg(va,void*);
    va_end(va);


    if(request ==FIONBIO)//如果命令是设置非阻塞模式
    {
        bool user_nonblock =!!*(int*)arg;//将整型数值转化为bool类型的安全写法
        std::shared_ptr<sylar::FdCtx> ctx =sylar::FdMgr::GetInstance()->get(fd);
        if(!ctx || ctx->isClosed() || !ctx->isSocket())
        {
            return ioctl_f(fd,request,arg);//直接调用原始函数
        }

        ctx->setUserNonblock(user_nonblock);//设置用户非阻塞标志
    }
    return ioctl_f(fd,request,arg);
}

//获取套接字选项值,直接使用原始函数
int getsockopt(int sockfd,int level,int optname,void *optval,socklen_t *optlen)
{
    return getsockopt_f(sockfd,level,optname,optval,optlen);
}

//设置套接字的选项，仅仅是获取一下超时信息
int setsockopt(int sockfd,int level,int optname,const void *optval,socklen_t optlen)
{
    if(!sylar::t_hook_enable)
    {
        return setsockopt_f(sockfd,level,optname,optval,optlen);
    }
    //处理SOL_SOCKET层的超时选项，就获取上下文的超时信息
    if(level == SOL_SOCKET)
    {
        //如果选项值是接收超时或者发送超时
        if(optname== SO_RCVTIMEO || optname==SO_SNDTIMEO)
        {
            std::shared_ptr<sylar::FdCtx> ctx =sylar::FdMgr::GetInstance()->get(sockfd);
            if(ctx)
            {
                const timeval *v=(const timeval*)optval;//转换为timeval结构体

                ctx->setTimeout(optname,v->tv_sec *1000+v->tv_usec/1000);//转换为毫秒
            }
        }
    }
    return setsockopt_f(sockfd,level,optname,optval,optlen);
}






} //extern "C"
