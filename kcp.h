/*************************************************************************
    > File Name: Kcp.h
    > Author: hsz
    > Brief:
    > Created Time: Mon 04 Jul 2022 11:25:28 AM CST
 ************************************************************************/

#ifndef __KCP_H__
#define __KCP_H__

#include <utils/mutex.h>
#include <utils/Buffer.h>
#include <libco/co_routine.h>
#include <arpa/inet.h>
#include <stdint.h>
#include <list>
#include <memory>
#include <functional>
#include "ikcp.h"

struct KcpAttr
{
    int32_t  fd;            // socket
    uint8_t  autoClose;     // whether to close automatically
    sockaddr_in addr;       // 
    uint32_t conv;          // conversation number
    uint16_t sendWndSize;   // send window size
    uint16_t recvWndSize;   // receive windows size
    uint8_t  nodelay;       // 0:disable(default), 1:enable
    int32_t  interval;      // internal update timer interval in millisec, default is 100ms
    uint8_t  fastResend;    // 0:disable fast resend(default), >0:enable fast resend

    KcpAttr() :
        fd(-1), autoClose(0), conv(0),
        sendWndSize(512), recvWndSize(512),
        nodelay(1), interval(100), fastResend(2)
    {
        memset(&addr, 0, sizeof(addr));
    }
};

class Kcp
{
    friend class KcpManager;
public:
    typedef std::shared_ptr<Kcp> SP;
    typedef std::function<void(eular::ByteBuffer &, sockaddr_in)> Callback;

    Kcp();
    Kcp(const KcpAttr &attr);
    ~Kcp();

    bool installRecvEvent(Callback onRecvEvent);
    void send(const eular::ByteBuffer &buffer);
    bool setAttr(const KcpAttr &attr);
    uint32_t check();

private:
    bool init();
    void bind(uint32_t tid);
    static int KcpOutput(const char *buf, int len, ikcpcb *kcp, void *user);
    void inputRoutine();
    void outputRoutine();

    struct KcpCompare {
        bool operator() (const Kcp::SP &v1, const Kcp::SP &v2)
        {
            if (v1 == nullptr) {
                return true;
            }

            if (v2 == nullptr) {
                return false;
            }

            assert(v1->mAttr.fd != v2->mAttr.fd);
            return static_cast<bool>(v1->mAttr.fd < v2->mAttr.fd);
        }
    };

private:
    ikcpcb          *mKcpHandle;
    uint32_t        mBindTid;
    KcpAttr         mAttr;

    Callback        mRecvEvent;
    eular::Mutex    mQueueMutex;
    std::list<eular::ByteBuffer> mSendBufQueue;
};

#endif // __KCP_FIBER_H__
