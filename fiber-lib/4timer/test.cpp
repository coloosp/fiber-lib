#include "timer.h"
#include<unistd.h>
#include <iostream>

using namespace sylar;

void func(int i)
{
    std::cout <<"执行业务回调函数,当前为定时器:" <<i<<std::endl;
}

int main(int argc,char *argv[])
{
    //创建定时器管理器对象
    std::shared_ptr<TimerManager> manager(new TimerManager());

    std::vector <std::function<void()>> cbs;//回调函数容器

    //测试非循环定时器
    {
        for(int i=0;i<10;i++)
        {
            manager->addTimer((i+1)*1000,std::bind(&func,i),false);
        }
        std::cout <<"定时器创建完成"<<std::endl;

        sleep(5);//休眠5秒等待前五个定时器超时

        manager->listExpiredCb(cbs);
        while(!cbs.empty())
        {
            std::function<void()> cb =*cbs.begin();//获取第一个回调函数
            cbs.erase(cbs.begin());//弹出队首
            cb();
        }

        //对后面五个进行同样的操作
        sleep(5);

        manager->listExpiredCb(cbs);

        while(!cbs.empty())
        {
            std::function<void()> cb=*cbs.begin();
            cbs.erase(cbs.begin());
            cb();
        }

    }

    //测试循环定时器
    {
        manager->addTimer(1000,std::bind(&func,1000),true);//true表示使用循环
        int j=10;
        while(j--) //循环十次，实际上这个定时器会一直在时间堆里面存在
        {
            sleep(1);//等待定时器超时
            manager->listExpiredCb(cbs);

            std::function<void()> cb=*cbs.begin();

            if(!cbs.empty())
            {
            cbs.erase(cbs.begin());
            cb();
            }

        }

    }

        return 0;


}