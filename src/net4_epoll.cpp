#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>

#include <thread>
#include <chrono>
#include <memory>
#include <utility>
#include <vector>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <sys/sendfile.h>
#include <sys/eventfd.h>

// https://www.freedesktop.org/software/systemd/man/systemd.socket.html
//   It should not invoke shutdown(2) on sockets it got with Accept=false
//   systemd note about shutdown(2) (forbidden)

#define NEVTS 12
#define XS_RCV_BUF_STA_SIZE 64536
#define XS_RCV_BUF_SPACE_LEFT_MIN 64536

template<class T>
using sp = ::std::shared_ptr<T>;

struct XsPacket
{
  uint8_t *data;
  size_t   dataLength;
};

enum XsSockType
{
  XS_SOCK_TYPE_LISTEN = 1,
  XS_SOCK_TYPE_NORMAL = 2,
  XS_SOCK_TYPE_EVENT = 3,
};

enum XsWriteOnlyType
{
  XS_WRITE_ONLY_TYPE_NONE = 0,
  XS_WRITE_ONLY_TYPE_BUFFER = 1,
  XS_WRITE_ONLY_TYPE_SENDFILE = 2,
};

struct XsWriteOnlyDataBuffer
{
  char       *mBuf;
  size_t      mLen;
  size_t      mOff;
};
int xs_write_only_data_buffer_advance(int Fd, struct XsWriteOnlyDataBuffer *Buffer)
{
  // caller inspects Buffer->mOff == Buffer->mLen for completion
  int nsent = 0;

  assert(Buffer->mOff <= Buffer->mLen);

  // FIXME: as performance opt, otherwise just call sendto with zero len
  if (Buffer->mOff == Buffer->mLen)
    return 0;

  nsent = sendto(Fd, Buffer->mBuf + Buffer->mOff, Buffer->mLen - Buffer->mOff, MSG_NOSIGNAL, NULL, 0);
  if (nsent == -1 && errno != EAGAIN && errno != EWOULDBLOCK)
    assert(0);
  if (nsent == -1)
    nsent = 0;

  Buffer->mOff += nsent;

  return 0;
}

// platform ifdefable
struct XsWriteOnlyDataSendFile
{
  int mFd;
  size_t mLen;
  size_t mOff;
};
int xs_write_only_data_send_file_advance(int Fd, struct XsWriteOnlyDataSendFile *SendFile)
{
  // caller inspects SendFile->mOff == SendFile->mLen for completion
  int nsent = 0;

  assert(SendFile->mOff <= SendFile->mLen);

  // FIXME: be smarter about requested count remember no such thing as nonblocking file read (linux)
  nsent = sendfile(Fd, SendFile->mFd, NULL, SendFile->mLen - SendFile->mOff);
  if (nsent == -1 && errno != EAGAIN)
    assert(0);
  if (nsent == -1)
    nsent = 0;

  SendFile->mOff += nsent;

  return 0;
}

struct XsWriteOnly
{
  enum XsWriteOnlyType mType;
  union
  {
    struct XsWriteOnlyDataBuffer mBuffer;
    struct XsWriteOnlyDataSendFile mSendFile;
  } mData;
};

struct XsRcvBuf
{
  size_t mBufSize;
  size_t mBufLen;
  char  *mBuf;
  size_t mBufOffset;
};

struct XsConCtx
{
  int mFd;
  struct XsWriteOnly mWriteOnly;
  struct XsRcvBuf mRcvBuf;
  /* must set the callbacks - caller inits other members
     likely want an extra context parameter for communication */
  int (*CbCtxCreate)(struct XsConCtx **oCtxBase, enum XsSockType Type);
  int (*CbCtxDestroy)(struct XsConCtx *CtxBase);
  int (*CbCrank)(struct XsConCtx *CtxBase, struct XsPacket *Packet);
  int (*CbWriteOnly)(struct XsConCtx *CtxBase);
};

typedef int (*xs_cb_ctx_create_t)(struct XsConCtx **oCtxBase, enum XsSockType Type);

struct XsEPollCtx
{
  enum XsSockType mType;
  struct XsConCtx *mCtx;
};

struct XsServCtl
{
  size_t mNumThread;
  std::vector<std::shared_ptr<std::thread> > mThread;
  int mEvtFdExitReq;
  int mEvtFdExit;
  int mEPollFd;
};

int cbctxcreate(struct XsConCtx **oCtxBase);
int cbctxdestroy(struct XsConCtx *CtxBase);
int cbcrank1(struct XsConCtx *CtxBase, struct XsPacket *Packet);
int cbwriteonly1(struct XsConCtx *CtxBase);

int xs_eventfd_read(int EvtFd)
{
  char Buf[8] = {};
  ssize_t nread = 0;
  while (-1 == (nread = read(EvtFd, Buf, 8))) {
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
      continue;
    assert(0);
  }
  assert(nread == 8);
  return 0;
}

int xs_eventfd_write(int EvtFd, int Value)
{
  uint64_t Val = (uint64_t) Value;
  char Buf[8] = {};
  ssize_t nwrite = 0;
  assert(sizeof Val == sizeof Buf);
  memcpy(Buf, &Val, 8);
  while (-1 == (nwrite = write(EvtFd, Buf, 8)))
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
      assert(0);
  assert(nwrite == 8);
  return 0;
}

int xs_serv_ctl_destroy(struct XsServCtl *ServCtl)
{
  if (ServCtl)
    delete ServCtl;

  return 0;
}

int xs_serv_ctl_quit_request(struct XsServCtl *ServCtl)
{
  if (!! xs_eventfd_write(ServCtl->mEvtFdExitReq, 1))
    assert(0);

  return 0;
}

int xs_serv_ctl_quit_wait(struct XsServCtl *ServCtl)
{
  if (!! xs_eventfd_read(ServCtl->mEvtFdExitReq))
    assert(0);

  if (!! xs_eventfd_write(ServCtl->mEvtFdExit, ServCtl->mNumThread))
    assert(0);

  for (size_t i = 0; i < ServCtl->mThread.size(); i++)
    ServCtl->mThread[i]->join();

  return 0;
}

int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type)
{
  struct XsConCtx *Ctx = new XsConCtx();

  Ctx->CbCtxCreate = cbctxcreate;
  Ctx->CbCtxDestroy = cbctxdestroy;
  Ctx->CbCrank = cbcrank1;
  Ctx->CbWriteOnly = cbwriteonly1;

  *oCtxBase = Ctx;

  return 0;
}

int cbctxdestroy(struct XsConCtx *CtxBase)
{
  struct XsConCtx *Ctx = (struct XsConCtx *) CtxBase;

  delete Ctx;

  return 0;
}

int cbcrank1(struct XsConCtx *CtxBase, struct XsPacket *Packet)
{
  struct XsConCtx *Ctx = (struct XsConCtx *) CtxBase;

  printf("pkt %.*s\n", (int) Packet->dataLength, Packet->data);

  Ctx->mWriteOnly.mType = XS_WRITE_ONLY_TYPE_BUFFER;
  Ctx->mWriteOnly.mData.mBuffer.mBuf = (char *) malloc(5);
  assert(Ctx->mWriteOnly.mData.mBuffer.mBuf);
  memcpy(Ctx->mWriteOnly.mData.mBuffer.mBuf, "ABCDE", 5);
  Ctx->mWriteOnly.mData.mBuffer.mLen = 5;
  Ctx->mWriteOnly.mData.mBuffer.mOff = 0;

  return 0;
}

int cbwriteonly1(struct XsConCtx *CtxBase)
{
  struct XsConCtx *Ctx = (struct XsConCtx *) CtxBase;

  printf("wol\n");

  struct XsWriteOnly *WriteOnly = &Ctx->mWriteOnly;

  switch (WriteOnly->mType)
  {

  case XS_WRITE_ONLY_TYPE_BUFFER:
  {
    struct XsWriteOnlyDataBuffer *Buffer = &WriteOnly->mData.mBuffer;

    if (!! xs_write_only_data_buffer_advance(Ctx->mFd, Buffer))
      assert(0);

    if (Buffer->mOff == Buffer->mLen) {
      free(Buffer->mBuf);
      Buffer->mBuf = NULL;
      WriteOnly->mType = XS_WRITE_ONLY_TYPE_NONE;
    }
  }
  break;

  case XS_WRITE_ONLY_TYPE_SENDFILE:
  {
    assert(0);
  }
  break;

  case XS_WRITE_ONLY_TYPE_NONE:
  default:
    assert(0);
  }

  return 0;
}

uint32_t xs_le_to_uint32(const char *buf, size_t bufsize)
{
  assert(sizeof(uint32_t) == 4 && bufsize == 4);
  uint32_t w = 0;
  w |= (buf[0] & 0xFF) << 0;
  w |= (buf[1] & 0xFF) << 8;
  w |= (buf[2] & 0xFF) << 16;
  w |= (buf[3] & 0xFF) << 24;
  return w;
}

int crank1readnet(struct XsConCtx *Ctx)
{
  bool DoneReading = 0;

  while (! DoneReading) {
    ssize_t nrecv = 0;
    assert(Ctx->mRcvBuf.mBufSize >= Ctx->mRcvBuf.mBufLen);
    size_t SpaceLeft = Ctx->mRcvBuf.mBufSize - Ctx->mRcvBuf.mBufLen;

    // FIXME: only grows, never shrinks
    if (SpaceLeft < XS_RCV_BUF_SPACE_LEFT_MIN) {
      char *NewBuf = NULL;
      if (! (NewBuf = (char *) realloc(Ctx->mRcvBuf.mBuf, Ctx->mRcvBuf.mBufSize * 2)))
	assert(0);
      Ctx->mRcvBuf.mBuf = NewBuf;
      Ctx->mRcvBuf.mBufSize *= 2;
      SpaceLeft = Ctx->mRcvBuf.mBufSize - Ctx->mRcvBuf.mBufLen;
    }

    // https://linux.die.net/man/7/tcp
    //   says MSG_TRUNC does bad stuff to stream sockets
    // recvfrom(2)
    //   lists some useful effects of MSG_TRUNC for other families
    if (-1 == (nrecv = recvfrom(Ctx->mFd, Ctx->mRcvBuf.mBuf + Ctx->mRcvBuf.mBufLen, SpaceLeft, MSG_DONTWAIT, NULL, NULL))) {
      if (errno != EAGAIN)
	assert(0);
      if (errno == EAGAIN)
	DoneReading = 1;
    }

    if (! DoneReading)
      Ctx->mRcvBuf.mBufLen += nrecv;
  }

  return 0;
}

int crank1readframe(struct XsConCtx *Ctx)
{
  const size_t FrameHdrLen = 5 /*MAGIC*/ + 4 /*len*/;
  size_t Offset = Ctx->mRcvBuf.mBufOffset;

  /* we can process frames until we are requested writeonly mode */
  while (Ctx->mRcvBuf.mBufLen - Offset >= FrameHdrLen) {
    if (!! memcmp(Ctx->mRcvBuf.mBuf + Offset, "FRAME", 5))
      assert(0);
    Offset += 5;
    const uint32_t FrameLen = xs_le_to_uint32(Ctx->mRcvBuf.mBuf + Offset, 4);
    Offset += 4;
    if (Ctx->mRcvBuf.mBufLen - Offset < FrameLen)
      break;
    /* have frame */
    struct XsPacket Packet = {};
    Packet.data = (uint8_t *) Ctx->mRcvBuf.mBuf + Offset;
    Packet.dataLength = FrameLen;
    /* handoff frame */
    Offset += FrameLen;
    assert(Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE);
    if (!! Ctx->CbCrank(Ctx, &Packet))
      assert(0);
    if (Ctx->mWriteOnly.mType != XS_WRITE_ONLY_TYPE_NONE)
      break;
  }

  Ctx->mRcvBuf.mBufOffset = Offset;

  return 0;
}

int accept_1(struct XsConCtx *OldCtx, int EPollFd)
{
  bool DoneAccepting = 0;

  while (! DoneAccepting) {
    int NewFd = -1;
    // FIXME: RIP ipv6
    struct sockaddr_in Addr = {};
    socklen_t AddrLen = sizeof Addr;
    
    if (-1 == (NewFd = accept4(OldCtx->mFd, (struct sockaddr *) &Addr, &AddrLen, SOCK_NONBLOCK | SOCK_CLOEXEC))) {
      int e = errno;
      /* this is not a failure of accept, but a pending error on new socket */
      /* see accept(2) (error handling section) */
      /* '.. should detect .. and treat then like EAGAIN ..' */
      // FIXME: have loggin on this case
      if (e == ENETDOWN || e == EPROTO || e == ENOPROTOOPT || e == EHOSTDOWN
	  || e == ENONET || e == EHOSTUNREACH || e == EOPNOTSUPP || e == ENETUNREACH)
	DoneAccepting = 1;
      else if (errno == EAGAIN || errno == EWOULDBLOCK)
        DoneAccepting = 1;
      else if (errno == ECONNABORTED)
	goto again;
      else
	assert(0);
    }
    assert(AddrLen <= sizeof Addr);

    if (! DoneAccepting) {
      struct XsEPollCtx *EPollCtx = NULL;
      struct XsConCtx   *Ctx = NULL;
      struct epoll_event NewEvt = {};

      EPollCtx = new XsEPollCtx();
    
      if (!! OldCtx->CbCtxCreate(&Ctx, XS_SOCK_TYPE_NORMAL))
        assert(0);
    
      Ctx->mFd = NewFd;
      Ctx->mRcvBuf.mBufSize = XS_RCV_BUF_STA_SIZE;
      Ctx->mRcvBuf.mBufLen = 0;
      Ctx->mRcvBuf.mBuf = (char *) malloc(XS_RCV_BUF_STA_SIZE);
      Ctx->mRcvBuf.mBufOffset = 0;
      Ctx->mWriteOnly.mType = XS_WRITE_ONLY_TYPE_NONE;

      assert(Ctx->mRcvBuf.mBuf);
    
      EPollCtx->mType = XS_SOCK_TYPE_NORMAL;
      EPollCtx->mCtx = Ctx;
    
      NewEvt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
      NewEvt.data.ptr = EPollCtx;
    
      if (epoll_ctl(EPollFd, EPOLL_CTL_ADD, Ctx->mFd, &NewEvt) == -1)
        assert(0);
    }

    again:
      (0 == 0);
  }
  
  return 0;
}

int crank1(struct XsConCtx *Ctx)
{
  if (!! crank1readnet(Ctx))
    assert(0);

  if (!! crank1readframe(Ctx))
    assert(0);

  return 0;
}

int writeonly1(struct XsConCtx *Ctx)
{
  if (!! Ctx->CbWriteOnly(Ctx))
    assert(0);

  if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
    if (!! crank1readframe(Ctx))
      assert(0);

  return 0;
}

void sender_func(struct XsServCtl *ServCtl)
{
  int ConnectFd = -1;

  struct sockaddr_in Addr = {};
  fd_set WSet;
  struct timeval TVal = {};
  int NReady = -1;
  int ConnectError = 0;
  socklen_t LenConnectError = sizeof ConnectError;

  if (-1 == (ConnectFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
    assert(0);

  Addr.sin_family = AF_INET;
  if (1 != inet_pton(AF_INET, "127.0.0.1", &Addr.sin_addr))
    assert(0);
  Addr.sin_port = htons(3384);

  while (-1 == connect (ConnectFd, (struct sockaddr *) &Addr, sizeof Addr)) {
    if (errno == EINTR)
      continue;
    if (errno == EINPROGRESS)
      break;
    assert(0);
  }

  FD_ZERO(&WSet);
  FD_SET(ConnectFd, &WSet);
  TVal.tv_sec = 5;
  TVal.tv_usec = 0;
  while (-1 == (NReady = select(ConnectFd+1, NULL, &WSet, NULL, &TVal))) {
    if (errno == EINTR)
      continue;
    assert(0);
  }

  if (NReady == 0 || ! FD_ISSET(ConnectFd, &WSet))
    assert(0);
  if (-1 == getsockopt(ConnectFd, SOL_SOCKET, SO_ERROR, &ConnectError, &LenConnectError))
    assert(0);
  if (ConnectError != 0)
    assert(0);

  while (1) {
    int nsent = 0;
    char buf[4096+1] = {};
    int nrecv = 0;

    int Offset = 0;

    assert(Offset + 5 <= 4096);
    memcpy(buf + Offset, "FRAME", 5);
    Offset += 5;
    assert(Offset + 4 <= 4096);
    memcpy(buf + Offset, "\x05\x00\x00\x00", 4);
    Offset += 4;
    assert(Offset + 5 <= 4096);
    memcpy(buf + Offset, "WORLD", 5);
    Offset += 5;

    if ((nsent = sendto(ConnectFd, buf, Offset, MSG_NOSIGNAL, NULL, 0)) == -1)
      assert(0);
    assert(nsent == Offset);

    nrecv = recvfrom(ConnectFd, buf, 4096, MSG_TRUNC | MSG_DONTWAIT, NULL, NULL);
    if (nrecv == -1 && errno != EAGAIN)
      assert(0);
    if (nrecv == -1 && errno == EAGAIN)
      buf[0] = '\0';
    if (nrecv > 4096)
      assert(0);
    printf(" s msg [%.*s]\n", nrecv, buf);

    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  }
}

void receiver_func(int Id, int EPollFd, int EvtFdExitReq)
{
  while (1)
  {
    struct epoll_event events[NEVTS] = {};
    int nready = 0;
    if ((nready = epoll_wait(EPollFd, events, NEVTS, 100)) == -1)
      assert(0);
    if (nready == 0)
      continue;
    printf("%d rdy %d\n", Id, nready);
    for (int i = 0; i < nready; i++)
    {
      struct XsEPollCtx *EPollCtx = (struct XsEPollCtx *) events[i].data.ptr;
      struct XsConCtx *Ctx = EPollCtx->mCtx;
      struct epoll_event newevt = {};
      if (events[i].events & EPOLLIN) {
	switch (EPollCtx->mType)
	{

	case XS_SOCK_TYPE_LISTEN:
	{
	  if (!! accept_1(Ctx, EPollFd))
	    assert(0);
	  newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	  newevt.data.ptr = EPollCtx;
	}
	break;

	case XS_SOCK_TYPE_NORMAL:
	{
	  assert(Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE);
	  if (!! crank1(Ctx))
	    assert(0);
	  if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
	    newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	  else
	    newevt.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
	  newevt.data.ptr = EPollCtx;
	  if (epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt) == -1)
	    assert(0);
	}
	break;

	case XS_SOCK_TYPE_EVENT:
	{
	  if (!! xs_eventfd_read(Ctx->mFd))
	    assert(0);
	  newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	  newevt.data.ptr = EPollCtx;
	  if (epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt) == -1)
	    assert(0);
	  // FIXME: resource handling
	  goto done;
	}
	break;

	default:
	  assert(0);
	}
      }
      else if (events[i].events & EPOLLOUT) {
	assert(EPollCtx->mType == XS_SOCK_TYPE_NORMAL);
	assert(Ctx->mWriteOnly.mType != XS_WRITE_ONLY_TYPE_NONE);
	if (!! writeonly1(Ctx))
	  assert(0);
	if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
	  newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	else
	  newevt.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
	newevt.data.ptr = EPollCtx;
	if (epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt) == -1)
	  assert(0);
      }
    }
  }

  done:
    (0 == 0);
}

int listenme(int ListenFd, xs_cb_ctx_create_t CbCtxCreate, struct XsServCtl **oServCtl)
{
  struct XsServCtl *ServCtl = NULL;

  std::vector<sp<std::thread> > ThreadRecv;

  size_t NumThread = 2;

  // thread requesting exit 0 -> 1 -> 0
  int EvtFdExitReq = -1;
  // controller (this) ordering exit 0 -> NumThread -> -=1 -> .. -> 0
  int EvtFdExit = -1;
  int EPollFd = -1;

  if (-1 == (EvtFdExitReq = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE)))
    assert(0);

  if (-1 == (EvtFdExit = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE)))
    assert(0);

  if ((EPollFd = epoll_create1(EPOLL_CLOEXEC)) == -1)
    assert(0);

  struct XsEPollCtx *EPollCtxEvt = new XsEPollCtx();
  struct XsConCtx   *CtxEvt = NULL;
  struct epoll_event EvtEvt = {};

  struct XsEPollCtx *EPollCtx = new XsEPollCtx();
  struct XsConCtx   *Ctx = NULL;
  struct epoll_event Evt = {};

  if (!! CbCtxCreate(&CtxEvt, XS_SOCK_TYPE_EVENT))
    assert(0);

  CtxEvt->mFd = EvtFdExit;
  EPollCtxEvt->mType = XS_SOCK_TYPE_EVENT;
  EPollCtxEvt->mCtx = CtxEvt;
  EvtEvt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
  EvtEvt.data.ptr = EPollCtxEvt;

  if (epoll_ctl(EPollFd, EPOLL_CTL_ADD, CtxEvt->mFd, &EvtEvt) == -1)
    assert(0);

  if (!! CbCtxCreate(&Ctx, XS_SOCK_TYPE_LISTEN))
    assert(0);

  Ctx->mFd = ListenFd;
  EPollCtx->mType = XS_SOCK_TYPE_LISTEN;
  EPollCtx->mCtx = Ctx;
  Evt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
  Evt.data.ptr = EPollCtx;

  if (epoll_ctl(EPollFd, EPOLL_CTL_ADD, Ctx->mFd, &Evt) == -1)
    assert(0);

  for (size_t i = 0; i < NumThread; i++)
    ThreadRecv.push_back(std::make_shared<std::thread>(receiver_func, i, EPollFd, EvtFdExitReq));

  ServCtl = new XsServCtl();
  ServCtl->mNumThread = NumThread;
  ServCtl->mThread = ThreadRecv;
  ServCtl->mEvtFdExitReq = EvtFdExitReq;
  ServCtl->mEvtFdExit = EvtFdExit;
  ServCtl->mEPollFd = EPollFd;

  if (oServCtl)
    *oServCtl = ServCtl;

  return 0;
}

int main(int argc, char **argv)
{
  int ListenFd = -1;

  struct sockaddr_in Addr = {};

  struct XsServCtl *ServCtl = NULL;
  sp<std::thread> ThreadSend;

  if (-1 == (ListenFd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
    assert(0);

  Addr.sin_family = AF_INET;
  if (! inet_aton("127.0.0.1", &Addr.sin_addr))
    assert(0);
  Addr.sin_port = htons(3384);

  if (-1 == bind(ListenFd, (struct sockaddr *) &Addr, sizeof Addr))
    assert(0);

  if (-1 == listen(ListenFd, SOMAXCONN))
    assert(0);

  if (!! listenme(ListenFd, cbctxcreate, &ServCtl))
    assert(0);

  ThreadSend = std::make_shared<std::thread>(sender_func, ServCtl);

  if (!! xs_serv_ctl_quit_wait(ServCtl))
    assert(0);

  xs_serv_ctl_destroy(ServCtl);

  return EXIT_SUCCESS;
}
