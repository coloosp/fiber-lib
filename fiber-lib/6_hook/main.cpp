#include "ioscheduler.h"
#include "hook.h"
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <stack>
#include <cstring>
#include <chrono>
#include <thread>

//全局变量，监听套接字文件描述符
static int sock_listen_fd =-1;

//处理客户端连接事件
void test_accept();

//错误处理函数
void error(const char *msg)
{
    perror(msg); //打印系统错误信息
    printf("error...\n");
    exit(1);//退出程序
}

//监听IO读事件
void watch_io_read()
{
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd,sylar::IOManager::READ,test_accept);
}

//处理客户端连接事件
void test_accept()
{
    //客户端地址结构体
    struct sockaddr_in addr;
    memset(&addr,0,sizeof(addr));//初始化地址结构体为0

    socklen_t len =sizeof(addr);

    //这里的accept已经是hook后的accept了
    int fd =accept(sock_listen_fd,(struct sockaddr*)&addr,&len);

    //accept失败
    if(fd <0)
    {
        std::cout <<"accept 失败,fd=" <<fd <<",errno =" <<errno <<std::endl;
    }
    else
    {
        std::cout <<"接收到连接,fd =" <<fd <<std::endl;

        //设置客户端fd为非阻塞模式
        fcntl(fd,F_SETFL,O_NONBLOCK);

        //为客户端fd添加READ事件,回调函数为lambda表达式
        sylar::IOManager::GetThis()->addEvent(fd,sylar::IOManager::READ,[fd]()
        {
            char buffer[1024];//接收缓冲区
            memset(buffer,0,sizeof(buffer));

            //循环读取客户端数据
            while(true)
            {
                //接收客户端数据，0是flags
                int ret =recv(fd,buffer,sizeof(buffer),0);

                if(ret >0)
                {
                    std::cout <<"接收到信息,fd=" <<fd<<",data=" <<buffer <<std::endl;
                     usleep_f(1000);//模拟阻塞1ms
                    //HTTP成功响应状态码，返回Nice try!
                    const char *response ="HTTP/1.1 200 OK\r\n"
                                           "Content-Type: text/plain\r\n"    
                                           "Content-Length: 10\r\n"          //表示10字节
                                           "Connection: keep-alive\r\n"      
                                           "\r\n"                            
                                           "Nice  try!";                  
                

                //发送HTTP响应
                ret=send(fd,response,strlen(response),0);

                close(fd);
                break;
                }

                 //读取数据失败或连接关闭
                if(ret <=0)
                  {   
                //客户端关闭或发送非EAGAIN(资源暂时不可用)错误
                if(ret ==0 || errno!=EAGAIN)
                {
                   std::cout <<"关闭连接,fd=" <<fd <<std::endl;
                   close(fd);
                   break;
                }
                else if(errno==EAGAIN)//资源暂时不可用
                {
                    //别着急，协程会自己挂起
                    std::cout <<"资源暂时不可用,fd=" <<fd <<std::endl;
                    // std:::this_thread::sleep_for(std::chrono::milliseconds(50));//延迟休眠时间，避免繁忙等待
                }
            }
            }//while(true) 循环读取
    });//addEvent lambda
    }//else 成功连接
    //触发事件后重新再手动添加，因为我们每次会移除事件并触发回调函数
    sylar::IOManager::GetThis()->addEvent(sock_listen_fd,sylar::IOManager::READ,test_accept);
}


//IO调度器测试函数
void test_iomanager()
{
    int portno =8080;//服务器监听端口
    struct sockaddr_in server_addr,client_addr;//服务器和客户端地址
    socklen_t client_len =sizeof(client_addr);//客户端地址长度

    //创建TCP套接字，传入IPv4，TCP协议，默认协议
    //hook后的socket会自动创建fd上下文
    sock_listen_fd =socket(AF_INET,SOCK_STREAM,0);

    if(sock_listen_fd <0)
    {
        error("失败创建socket..\n");
    }

    //解决"address already in use"错误，设置端口复用
    int yes =1;
    setsockopt(sock_listen_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));

    //初始化服务器地址结构体
    memset((char*)&server_addr,0,sizeof(server_addr));
    server_addr.sin_family =AF_INET;//地址族IPv4
    server_addr.sin_port =htons(portno);//端口号
    server_addr.sin_addr.s_addr = INADDR_ANY;//绑定所有网卡IP

    //绑定套接字并监听连接,这里并非绑定回调的std::bind
    if(bind(sock_listen_fd,(struct sockaddr *)&server_addr,sizeof(server_addr)) <0)
        error("绑定套接字错误...\n");

        //监听连接
    if(listen(sock_listen_fd,1024) <0)
    {
        error("监听连接错误...\n");
    }

    printf("epoll echo server listening for connections on port :%d\n",portno);

    fcntl(sock_listen_fd,F_SETFL,O_NONBLOCK);//设置监听fd为非阻塞模式

    sylar::IOManager iom(2);// 2个工作线程，这是因为我的服务器有点垃圾，各位可以根据自己的服务器决定最佳线程数

    //有客户端连接时触发test_accept回调
    iom.addEvent(sock_listen_fd,sylar::IOManager::READ,test_accept);
}

int main(int argc,char *argv[])
{
    test_iomanager();
    return 0;
}
