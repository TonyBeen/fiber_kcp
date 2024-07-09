/*************************************************************************
    > File Name: kcp_context.h
    > Author: hsz
    > Brief:
    > Created Time: Thu 06 Jun 2024 10:41:35 AM CST
 ************************************************************************/

#ifndef __KCP_CONTEXT_H__
#define __KCP_CONTEXT_H__

#include <stdint.h>
#include <list>
#include <memory>
#include <functional>

#include <moodycamel/readerwriterqueue.h>

#include <utils/mutex.h>
#include <utils/buffer.h>
#include <utils/sysdef.h>

#include "kcp_setting.h"

struct IKCPCB;

namespace eular {
class KcpContext;
typedef std::function<void(std::shared_ptr<KcpContext>, const eular::ByteBuffer &)>   ReadEventCB;

class KcpContext : public std::enable_shared_from_this<KcpContext>
{
    friend class KcpServer;
    friend class KcpClient;
    typedef std::function<void(std::shared_ptr<KcpContext>, bool)>   ContextCloseCB;

public:
    typedef std::shared_ptr<KcpContext> SP;

    KcpContext();
    ~KcpContext();

    void installRecvEvent(ReadEventCB onRecvEvent);
    bool send(const eular::ByteBuffer &buffer);
    bool send(eular::ByteBuffer &&buffer);
    void closeContext();
    void resetContext();

    const String8&  getLocalHost() const;
    uint16_t        getLocalPort() const;
    const String8&  getPeerHost() const;
    uint16_t        getPeerPort() const;

protected:
    // 设置配置
    void setSetting(const KcpSetting &setting);

    // KCP发送接口
    static int KcpOutput(const char *buf, int len, IKCPCB *kcp, void *user);

    // ikcp_update超时回调
    void onUpdateTimeout();

    // 收到数据
    void onRecv(const eular::ByteBuffer &inputBuffer);

    // 重置
    void reset();

protected:
    String8         m_peerHost;
    uint16_t        m_peerPort;
    String8         m_localHost;
    uint16_t        m_localPort;
    uint64_t        m_timerId;      // kcp发送数据定时器ID

private:
    IKCPCB*         m_kcpHandle;
    ByteBuffer      m_recvBuffer;
    KcpSetting      m_setting;
    ReadEventCB     m_recvEvent;
    ContextCloseCB  m_closeEvent;

    using LockFreeQueue = moodycamel::BlockingReaderWriterQueue<eular::ByteBuffer>;
    LockFreeQueue   m_sendBufQueue;
};

} // namespace eular

#endif // __KCP_CONTEXT_H__
