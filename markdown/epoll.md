# epoll
稍微谈谈我说了解的epoll到底是什么？
[图文并茂讲解epoll原理，彻底弄懂epoll机制](https://blog.csdn.net/m0_60259116/article/details/134332371)
这篇文章对我的学习帮助很多，可以学习的,作者后续的说明很多也是抄的他的(差不多是这个意思)

首先,先下结论，epoll是在linux内核提供的IO多路复用机制

## epoll_create
调用epoll_create(size_t t)时，内核会创建一个eventpoll对象的结构体，其中包含了红黑树，就绪队列和等待队列，至于这些内部结构到底有什么作用不必了解。

调用epoll_create(size_t t)会返回epfd作为文件描述符(这是linux的句柄)，其中t并没有意义，只要大于零即可

我们来说明epoll的核心作用：通过内核维护的红黑树高效管理大量文件描述符(fd)，为每个fd注册内核回调函数，当fd的IO状态发生变化时，会触发回调，将就绪的fd加入就绪队列；

更简短地说epoll起到了监控的作用，帮程序监控一批文件描述符（fd）的 IO 事件（读 / 写 / 错误），并在事件就绪时主动通知程序，避免程序无意义地轮询所有 fd


## epoll_event
epoll_event是一个结构体，内部包含了events与data
我们知道epoll的用处是监控/监听。我们把data描述为监听对象，events可以描述为监听对象data的监听事件集
所以我们做的是监听data中的对应事件的IO变化情况
### events
类型是`uint32_t`
是EPOLL_EVENT的或运算事件集合，以下列出EPOLL_EVENT的枚举宏定义
```
enum EPOLL_EVENTS
{
    EPOLLIN = 0x001, //读事件
    EPOLLPRI = 0x002,
    EPOLLOUT = 0x004, //写事件
    EPOLLRDNORM = 0x040,
    EPOLLRDBAND = 0x080,
    EPOLLWRNORM = 0x100,
    EPOLLWRBAND = 0x200,
    EPOLLMSG = 0x400,
    EPOLLERR = 0x008, //出错事件
    EPOLLHUP = 0x010, //挂起事件
    EPOLLRDHUP = 0x2000,
    EPOLLEXCLUSIVE = 1u << 28,
    EPOLLWAKEUP = 1u << 29,
    EPOLLONESHOT = 1u << 30,
    EPOLLET = 1u << 31 //边缘触发
```
例如我的事件是一个既需要读又需要写的事件集合
events =EPOLLIN | EPOLLOUT；

其他的运算比如可以通过&运算来判断一个事件集合中是否有对应运算
比如判断events &EPOLLIN是否等于0来判断是否包含了该事件(0为不包含)

比如通过events =events |~EPOLLINl;来删除读事件

### data
data是一个联合体，可以每一个epoll_event可以表示以下内容其中一个
```
typedef union epoll_data {  
    void *ptr;  
    int fd;  //文件描述符
    uint32_t u32;  
    uint64_t u64;
}
```
在我们代码中既又对管道文件的data也有对文件上下文指针的data

内核会提取epoll_event的核心信息存入epoll的红黑树结构中的红黑树节点epitem中
## epoll_ctl
epoll_ctl(epfd,op,fd,epoll_event)用于对epoll_event的增删改
epfd是epoll的文件描述符
op是操作，可以有以下几个
```
EPOLL_CTL_ADD:插入事件
EPOLL_CTL_DEL:删除事件
EPOLL_CTL_MOD:修改事件
```
fd是epoll_event绑定的套接字文件描述符


原来是用 *event 里的 events 字段（即 event->events），修改内核中与 fd 绑定的 epitem 节点里的 event.events 掩码

## epoll_wait
epoll_wait(epfd,epoll_event数组指针，maxevent,timeout) 用于监听套接字事件
maxevents是epoll_event长度
timeout:超时时间
如果小于0会一直等待
如果等于0立刻返回
如果大于0会等待对应超时时间，单位为毫秒(阻塞模式，我们项目就使用这个)

返回值：
小于0表示出错
等于0表示超时
大于0表示阻塞时间内接收到就绪事件的个数
