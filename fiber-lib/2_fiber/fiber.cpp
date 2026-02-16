#include "fiber.h"

//调试模式
static bool debug =true;

namespace sylar{

//当前协程的裸指针
static thread_local Fiber* t_fiber =nullptr;

//主协程的智能指针
static thread_local std::shared_ptr<Fiber> t_thread_fiber =nullptr;

//设置线程的调度协程裸指针
static thread_local Fiber* t_scheduler_fiber = nullptr;

//全局原子操作变量 协程ID计数器
static std::atomic<uint64_t> s_fiber_id{0};

//当前程序创建总协程数量计数器
static std:: atomic<uint64_t> s_fiber_count{0};

//设置当前运行协程
void Fiber::SetThis(Fiber *f)
{
    t_fiber =f; //将协程指针传给线程局部变量
}

//获取当前协程的智能指针
std::shared_ptr<Fiber> Fiber::GetThis()
{
    //如果当前线程已经有运行的协程
    if(t_fiber)
    {
        //这个方法是模板类继承的，用于通过裸指针安全获得智能指针
        return t_fiber->shared_from_this(); 
    }

    //当前线程第一次调用该函数，说明还没有创建主协程
    else {
        //构造主线程对象
        //这种写法是因为make_shared方法并不能调用私有成员
         std::shared_ptr<Fiber> main_fiber(new Fiber());


         t_thread_fiber =main_fiber;

         //get函数是智能指针自带的函数，作用是返回裸指针
         t_scheduler_fiber=main_fiber.get();

         //确保当前运行协程指针指向刚创建的主协程
         assert(t_fiber == main_fiber.get());

         //返回智能指针
         return t_fiber ->shared_from_this();

    }

}

void Fiber::SetSchedulerFiber(Fiber* f)
{
	t_scheduler_fiber = f;
}

uint64_t Fiber::GetFiberId()
{
    //存在有运行的协程，直接返回ID
    if(t_fiber)
    {
        
        return t_fiber->getId();
    }
    else{
        //无运行协程，返回无效ID
        return (uint64_t)-1;
    }
}

//用于创建主协程的私有构造
Fiber::Fiber()
{
    SetThis(this); //传给线程局部变量

    m_state =RUNNING; //主协程维持运行状态

    //获取当前上下文，保存在主协程的m_ctx中
    //返回0成功，非0失败
    if(getcontext(&m_ctx))
    {
        std::cerr <<"获取上下文失败，协程主协程创建失败\n";
        pthread_exit(NULL);
    }

    m_id =s_fiber_id++;//主协程id

    s_fiber_count ++;//全局协程总数+1

    //调试模式下打印主协程id
    if(debug) 
        std:: cout <<"创建主协程id" << m_id <<std::endl;
}

//创建子协程函数,传入回调函数，栈大小，是否交给调度协程
Fiber::Fiber(std::function <void()> cb, size_t stacksize, bool run_in_scheduler):
m_cb(cb),m_runInScheduler(run_in_scheduler)
{
    m_state =READY;//子协程初始为准备状态

    //若输入非0选择输入栈空间大小，否则采用默认值
    m_stacksize=stacksize ? stacksize :128000;

    m_stack=malloc(m_stacksize);//申请空间，并且返回对应指针

    //获取上下文
    if(getcontext(&m_ctx))
    {
        std::cerr <<"子协程创建失败，获取上下文失败\n";
        pthread_exit(NULL);
    }

    m_ctx.uc_link =nullptr;//表示协程没有后继上下文，需要手动调用yield来让出执行权

    m_ctx.uc_stack.ss_sp=m_stack;//指定协程栈内存的起始地址

    m_ctx.uc_stack.ss_size = m_stacksize;//设置协程栈内存大小

    //修改m_ctx的上下文，当协程执行resume恢复时，会跳转到MainFunc函数开始执行
    makecontext(&m_ctx,&Fiber::MainFunc,0);

    //协程id自增
    m_id=s_fiber_id++;

    //协程计数器自增
    s_fiber_count++;

    //调试模式
    if(debug) 
        std::cout <<"创建子协程id :" <<m_id <<std::endl;

}

//实现析构函数
Fiber::~Fiber()
{

//协程计数器自减
s_fiber_count--;

if(m_stack)
{
    //释放对应栈空间
    free(m_stack);
}

if(debug)
    std::cout <<"销毁协程id " <<m_id <<std::endl;
}

//重置协程的回调函数
void Fiber::reset(std::function<void()> cb)
{
    //需要有独立栈内存且为结束态的协程才能重置
    assert(m_stack !=nullptr&&m_state==TERM);

    m_state=READY;//重置为就绪态

    m_cb=cb;//重置更新回调函数

    //重新绑定上下文
    if(getcontext(&m_ctx)){
        std::cerr << "获取上下文失败，回调函数重置失败\n";
        pthread_exit(NULL);
    }

    m_ctx.uc_link =nullptr;

    m_ctx.uc_stack.ss_sp=m_stack;

    m_ctx.uc_stack.ss_size = m_stacksize;

    makecontext(&m_ctx,&Fiber::MainFunc,0);

}

//恢复执行协程
void Fiber::resume()
{

    assert(m_state==READY);//只有就绪态的协程才能恢复执行

    m_state=RUNNING;//切换为运行态

    //如果当前协程需要交给调度协程调度
    if(m_runInScheduler)
    {
        SetThis(this);//标记当前运行协程为本协程，将执行权交给本协程

        //上下文切换，把CPU执行权从调度协程切换到当前子协程
        //调度协程的上下文被保存，子协程的上下文被加载
        if(swapcontext(&(t_scheduler_fiber->m_ctx),&m_ctx))
        {
            std::cerr <<"上下文切换失败,当前协程恢复失败\n";
            pthread_exit(NULL); 
        }

    }
    //无调度协程，说明主协程作为调度协程
    else{
        SetThis(this);

        if(swapcontext(&(t_thread_fiber->m_ctx),&m_ctx))
        {
            std::cerr <<"上下文切换失败,当前协程恢复失败\n";
            pthread_exit(NULL); 
        }
    }
}

//让出CPU执行权，暂停运行
void Fiber::yield()
{
    //只有处于运行态或者结束态的协程才能让出执行权
    assert(m_state==RUNNING || m_state==TERM);

    //如果协程不是结束态，说明是主动暂停，将状态修改为就绪态，等待下次调用
    if(m_state!=TERM)
    {
        m_state=READY;
    }

    //同样检查是否需要交给调度协程，但操作与resume相反
    if(m_runInScheduler)
    {
        SetThis(t_scheduler_fiber);//交给调度协程

        if(swapcontext(&m_ctx,&(t_scheduler_fiber->m_ctx)))
        {
            std::cerr <<"上下文切换失败,协程暂停失败\n";
            pthread_exit(NULL);
        }

    }
    else
    {
        SetThis(t_thread_fiber.get());//交给主协程,因为这里是智能指针，需要传入裸指针

        if(swapcontext(&m_ctx,&(t_thread_fiber->m_ctx)))
        {
            std::cerr <<"上下文切换失败,协程暂停失败\n";
            pthread_exit(NULL);
        }
    }
}

void Fiber::MainFunc()
{
    std::shared_ptr<Fiber> curr =GetThis();//获取当前协程的智能指针

    //是否获取有效协程指针
    assert(curr!=nullptr);

    curr->m_cb();//执行传入的回调函数，这是协程核心要做的事情

    curr->m_cb = nullptr;//执行完成后清空回调函数

    curr->m_state=TERM;//执行完成后进入结束态

    auto raw_ptr =curr.get();//获取当前协程裸指针,因为curr要被重置了

    //重置智能指针,释放对当前协程的管理，这是智能指针自带的方法，并非上面的回调函数重置方法
    //因为在这里智能指针离开作用域就会释放内存，重置智能指针以防止
    curr.reset();

    raw_ptr->yield();//调用yield暂停，让出执行权


}




}