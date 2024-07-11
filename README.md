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


#### 见develop
1、Master设计只支持一个socket对应一个kcp, 如此太过浪费资源, 将其修改为一个循环拥有多个KCP(此处KCP可理解为一个UDP服务器)
    一个KCP拥有最多255个业务kcp会话, 0x4B435000作为初始化会话Id, 用于建立连接(kcp不同于utp, 其不支持连接操作)
    会话ID范围 0x4B435000 - 0x4B4350FF (KCP0 - KCP255)

2、考虑到公网环境下udp可接收任何pc的数据, 当存在恶意发送udp数据时会导致kcp数据异常, 使得kcp重发几率增高
    解决办法:
    udp取数据时按包取, 只要不超过MTU, 就不会被分片, 故KCP发送时严格按照MTU大小发送, KCP头部4字节为conv,
    所以校验头部是否满足即可区分有效数据, 无效数据丢弃