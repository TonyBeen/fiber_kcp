##### 架构

https://github.com/skywind3000/kcp


#### 依赖
> https://github.com/TonyBeen/eular
> utils & log


#### 扩展
> 如果想复用一个udp套接字，则只需要在kcp内部实现一个交流号到客户端(抽象，可以是sockaddr_in, 也可以是ikcpcb *)的映射
> 当回调到读事件时，读取全部文本，并将文本按kcp协议块进行区分，分别发送给映射，如果存在用户数据，则回调到用户
> 大致流程就是 连接协议(连接并服务端分配交流号) -> 数据交互(心跳检测) -> 断连协议(服务端清除相关内存)
                                                    |
                                                    v
                                            心跳未响应回调到服务端失连

#### 测试
`https://blog.csdn.net/DefiniteGoal/article/details/125786642`