#include "fiber.h"
#include<vector>

using namespace sylar;

//协程任务调度器,这里实现了一个简易的任务管理器，我们后续会实现一个完整的
class Scheduler
{
public:

    //将待执行协程加入任务队列
    void schedule(std::shared_ptr<Fiber> task)
    {
        m_tasks.push_back(task);
    }

    void run()
    {
        std::cout <<"当前任务队列协程数：" <<m_tasks.size() <<std::endl;

        std::shared_ptr<Fiber> task;//协程智能指针

        auto it =m_tasks.begin();//任务队列起始迭代器

        while(it!=m_tasks.end())
        {
            task =*it;//迭代器解引用，获取当前位置的智能指针

            task->resume();//恢复当前协程，执行当前协程回调函数
            it++;//迭代器自增后移
        }
        m_tasks.clear();
    }

private:
    std::vector<std::shared_ptr<Fiber>> m_tasks;//任务队列

};

//协程要执行的业务函数
void test_fiber(int i)
{
    std::cout <<"这里是测试函数,当前执行协程：" << i<<std::endl;
}

int main()
{
    Fiber::GetThis();//第一次调用会创建主协程

    Scheduler sc;//调度器实例

    for(int i=0;i<20;i++)
    {   
        //bind可以生成可调用的回调函数对象
        //传入回调函数，栈内存（这里是默认）,不使用调度协程（交给主协程管理）
        std::shared_ptr<Fiber> fiber =std::make_shared<Fiber>(std::bind(test_fiber,i) ,0, false);

        sc.schedule(fiber);//将任务加入任务队列
    }
    sc.run();

    return  0;


}
