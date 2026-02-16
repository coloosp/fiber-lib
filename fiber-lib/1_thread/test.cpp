#include "thread.h"
#include <iostream>
#include <memory>
#include <vector>
#include <unistd.h>

using namespace sylar;

//线程的业务执行函数，也就是我们传入的回调函数cb
void func()
{

//方式1 通过Thread的静态函数获取系统id和线程名
std::cout <<"id: " <<Thread::GetThreadId() <<", name=" << Thread::GetName()<<std::endl;

//方式2 通过Thread指针获取系统id和线程名
std::cout <<", this id:" <<Thread::GetThis()->getId() <<", this name:" <<Thread::GetThis()->getName()<<std::endl;

//线程休眠60秒，方便使用ps命令测试线程是否真实成功创建
sleep(60);


}

int main()
{
    //线程类数据类型智能指针数组
std::vector<std::shared_ptr<Thread>> thrs;

//创建5个线程
for(int i=0;i<5;i++)
{
    //创建指向线程的智能指针
 std::shared_ptr<Thread> thr =std::make_shared<Thread>(&func,"线程"+std::to_string(i));
    //加入数组中
 thrs.push_back(thr);

}


//等待所有线程执行完毕
for(int i=0;i<5;i++)
{
    //先创建全部线程让其并发运行，在统一等待
    thrs[i]->join();
    //你浪费一个人的60s在并发操作里面依旧是60s
}




return 0;
}
