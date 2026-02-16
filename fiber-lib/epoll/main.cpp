#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/epoll.h>

#define MAX_EVENTS 10 
#define PORT 8080

//因为不在一个文件夹，以下函数均为系统原始函数，非hook后的函数
int main()
{
    //分别是监听socket，客户端socket,epoll实例的文件描述符，与在epoll_wait中返回的就绪事件数
    int listen_fd,conn_fd,epoll_fd,event_count;

    struct sockaddr_in server_addr,client_addr; //地址结构体
    socklen_t addr_len =sizeof(client_addr); 

    struct epoll_event events[MAX_EVENTS],event;//epoll_event数组和epoll_event实例

    if((listen_fd =socket(AF_INET,SOCK_STREAM,0)) ==-1)
    {
        perror ("socket");//系统错误打印函数
        return -1;
    }

    int yes =1;
    setsockopt(listen_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));


    //初始化服务器结构体
    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family=AF_INET; //IPv4
    server_addr.sin_port =htons(PORT); //端口8080
    server_addr.sin_addr.s_addr =INADDR_ANY; //绑定所有网卡

    //绑定监听socket到指定端口
    if(bind(listen_fd,(struct sockaddr*)&server_addr,sizeof(server_addr))==-1)
    {
        perror("bind");
        return  -1;
    }

    //监听连接
    if(listen(listen_fd,1024) ==-1)
    {
        perror("listen");
        return -1;
    }

    //创建epoll实例，epoll_creat1为epoll_create的增强版，0表示默认配置
    if((epoll_fd =epoll_create1(0))==-1)
    {
        perror("epoll_create1");
        return -1;
    }

    //设置epoll_event
    event.events =EPOLLIN;//监听事件为读事件
    event.data.fd =listen_fd;//关联监听套接字

    //将epoll_event添加到epoll
    if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,listen_fd,&event)==-1)
    {
        perror("epoll_ctl");
        return -1;
    }

    //核心逻辑
    while(true)
    {
        //等待有就绪事件发生，是不是和我们的idle函数那边很像？
        event_count =epoll_wait(epoll_fd,events,MAX_EVENTS,-1);

        if(event_count ==-1)
        {
            perror("epoll_wait");
            return -1;
        }

        //遍历找出所有就绪事件
        for(int i=0;i<event_count;i++)
        {   
            //如果这个位置是连接事件，就添加监听数据的事件
            if(events[i].data.fd ==listen_fd)
            {
                conn_fd =accept(listen_fd,(struct sockaddr*)&client_addr,&addr_len);
                if(conn_fd ==-1)
                {
                    perror("accept");
                    continue; //当前错误连接，接着处理下一个事件
                }

                event.events =EPOLLIN; //读事件
                event.data.fd =conn_fd;//监听客户端
                if(epoll_ctl(epoll_fd,EPOLL_CTL_ADD,conn_fd,&event) ==-1)
                {
                    perror("epoll_ctl");
                    return -1;
                }
            }
            else //读数据事件
            {
                char buf[1024];
                //读取客户端数据
                int len =read(events[i].data.fd,buf,sizeof(buf)-1);
                
                //读取失败或者客户端关闭连接
                if(len <=0)
                {
                    close(events[i].data.fd);
                }
                else //读取到数据
                {
                    //HTTP响应，打出GG
                     usleep(1000);//模拟阻塞
                    const char *response ="HTTP/1.1 200 OK\r\n"
                                          "Content-Type: text/plain\r\n"
                                          "Content-Length:10\r\n"
                                          "Connection: keep-alive\r\n"
                                          "\r\n"
                                          "Good Game!";

                    write(events[i].data.fd,response,strlen(response));

                    close(events[i].data.fd);
                }
            }
        }
    }
    close(listen_fd);
    close(epoll_fd);
    return 0;
}