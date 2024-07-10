##### 架构

https://github.com/skywind3000/kcp


#### 依赖
> https://github.com/TonyBeen/eular
> utils & log


#### 测试
`https://blog.csdn.net/DefiniteGoal/article/details/125786642`

### Server套接字复用
#### SYN
    1、建立连接
        客户端主动发送SYN建立连接, 服务端回复ACK, 服务端将客户端信息放入半连接队列,
        如果在指定时间内未向服务端发送数据或请求, 在超时将其从半连接队列移除并发送FIN

        如果会话号已满, 则发送FIN给客户端, 并设置会话号为KCP_FLAG

    2、断开连接
        (1)、常规断连
            客户端或服务器主动发送FIN, 将其放入半连接队列, 此时双方可正常收发数据,
            当收到客户端的ACK后, 关闭KCP

            客户端主动发送FIN时, 立马回复ACK, 并销毁KCP实例

        (2)、快速断连
            服务器/客户端发送RST, 对端收到后直接关闭KCP, 无需回复ACK

    3、服务端同一套接字支持最大255个会话
