#include "scheduler.h"

using namespace sylar;

static unsigned int test_number =0;

std::mutex mutex_cout;//互斥锁

//任务函数
void task()
{

{
    std::lock_guard<std::mutex> lock(mutex_cout);

    std::cout <<"正在执行任务," <<test_number++ <<"处于线程:" << Thread::GetThreadId() <<std::endl;

}
sleep(1);//休眠一秒

}

int main(int argc,char const *argv[])
{
    {
    //构造调度器，总工作线程3，使用主线程作为工作线程
    std::shared_ptr<Scheduler> scheduler =std::make_shared<Scheduler> (3,true,"调度器1");

    scheduler->start();

    sleep(2);

    std::cout << "首次提交任务\n\n";

    for(int i=0;i<5;i++)
    {
        //任务，创建协程对象，将task函数封装在任务中
        std::shared_ptr<Fiber> fiber =std::make_shared <Fiber>(task);

        //添加任务到任务队列中,这里我们并未指定线程id，会使用默认值-1，这实际上说明任务是线程竞争执行的
        scheduler->scheduleLock(fiber);
    }

    sleep(6);

    std::cout <<"再次提交任务\n\n";

    for(int i=0;i<15;i++)
    {
        std::shared_ptr<Fiber> fiber =std::make_shared<Fiber>(task);

        scheduler->scheduleLock(fiber);
    }

    sleep(3);

    scheduler->stop();

    }

}