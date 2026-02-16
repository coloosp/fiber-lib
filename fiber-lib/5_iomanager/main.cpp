#include"ioscheduler.h"
#include<iostream>
#include<sys/socket.h>
#include<netinet/in.h>
#include<arpa/inet.h>
#include<fcntl.h>
#include<unistd.h>
#include<sys/epoll.h>
#include<cstring>
#include<cerrno>


using namespace sylar;

char recv_data[4096];//全局缓冲区

const char data[]="GET / HTTP/1.0\r\n\r\n";//这是最简请求报文，作用是请求根目录资源，也就会获取首页资源

int sock;//全局socket文件描述符

//回调函数，处理读事件
void func()
{
    recv(sock,recv_data,4096,0);//从sock中读取数据到recv_data缓冲区
    //打印接收到的recv_data
    std::cout <<recv_data << std::endl <<std::endl;
}

//处理写事件
void func2()
{
    send(sock,data,sizeof(data),0);

}

int main(int argc,char const *argv[])
{
    IOManager manager(2);//两个工作线程，其他默认

    //创建TCP socket
    //使用IPv4协议，TCP流式套接字，默认协议(TCP)
    sock =socket(AF_INET,SOCK_STREAM,0);

    //服务器地址结构体
    sockaddr_in server;
    server.sin_family =AF_INET; //地址族为IPv4
    server.sin_port=htons(80);//端口80
    server.sin_addr.s_addr=inet_addr("103.235.46.96"); //服务器IP地址，这是百度IP，因为百度对应http的请求相对宽松

    fcntl(sock,F_SETFL,O_NONBLOCK);//设置socket为非阻塞模式

    //发起非阻塞连接
    connect(sock,(struct sockaddr *)&server,sizeof(server));

    //向IO调度器注册事件
    manager.addEvent(sock,IOManager::WRITE,&func2);//注册写事件，绑定了回调函数func2
    manager.addEvent(sock,IOManager::READ,&func);//注册读事件，绑定了回调函数func

    std::cout <<"事件已提交到IO调度器";
    //这里虽然主函数结束了，但是IO调度器依旧在执行
    return 0;



}