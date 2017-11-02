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
#include <sys/random.h>      // getrandom (consider getentropy for BSD compatibility?)

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/filesys.h>
#include <gittest/net4.h>
#include <gittest/gittest.h> // aux_LE_to_uint32
#include <gittest/frame.h>   // aux_frame_part_write_for_payload_lowlevel

// https://www.freedesktop.org/software/systemd/man/systemd.socket.html
//   It should not invoke shutdown(2) on sockets it got with Accept=false
//   systemd note about shutdown(2) (forbidden)

#define NEVTS 12
#define XS_RCV_BUF_STA_SIZE 64536
#define XS_RCV_BUF_SPACE_LEFT_MIN 64536

/* intended to be forward-declared in header (API use pointer only) */
struct XsServCtl
{
	size_t mNumThread;
	std::vector<std::shared_ptr<std::thread> > mThread;
	int mEvtFdExitReq;
	int mEvtFdExit;
	int mEPollFd;
};

struct XsEPollCtx
{
	enum XsSockType mType;
	struct XsConCtx *mCtx;
};

// FIXME: temp func
void sender_func(struct XsServCtl *ServCtl);

// FIXME: temp funcs
static int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type, struct XsConExt *Ext);
static int cbctxdestroy(struct XsConCtx *CtxBase);
static int cbcrank1(struct XsConCtx *CtxBase, struct GsPacket *Packet);
static int cbwriteonly1(struct XsConCtx *CtxBase);

static int xs_eventfd_read(int EvtFd);
static int xs_eventfd_write(int EvtFd, int Value);

static int crank1readnet(struct XsConCtx *Ctx);
static int crank1readframe(struct XsConCtx *Ctx);

static int accept_1(struct XsConCtx *OldCtx, int EPollFd);
static int crank1(struct XsConCtx *Ctx);
static int writeonly1(struct XsConCtx *Ctx);

static void receiver_func(int Id, int EPollFd, int EvtFdExitReq);

struct GsNet4DumpRemoteData { uint32_t Tripwire; int Fd; int IsError; size_t Limit; };
static int gs_net4_dump_remote_cb(void *ctx, const char *d, int64_t l);
#define GS_TRIPWIRE_NET4_DUMP_REMOTE_DATA 0x48D09680

int gs_net4_dump_remote_cb(void *ctx, const char *d, int64_t l)
{
	int r = 0;

	/* should be callable from a signal context (ex see signal-safety(7)) */

	struct GsNet4DumpRemoteData *Data = (GsNet4DumpRemoteData *)ctx;

	size_t Offset = 0;

	if (Data->Tripwire != GS_TRIPWIRE_LOG_CRASH_HANDLER_DUMP_BUF_DATA)
		return 1;

	if (Data->IsError)
		return 1;

	while (Offset < l) {
		ssize_t nsent = 0;
		while (-1 == (nsent = sendto(Data->Fd, d + Offset, l - Offset, MSG_NOSIGNAL, NULL, 0))) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			} else {
				Data->IsError;
				return 1;
			};
		}
		Offset += nsent;
	}

	return 0;
}

int gs_net4_crash_handler_log_dump_remote(
	const char *IdTokenBuf, size_t LenIdToken,
	uint32_t AddrHostByteOrder, uint32_t PortHostByteOrder,
	size_t ConnectTimeoutSec)
{
	int r = 0;

	/* should be callable from a signal context (ex see signal-safety(7)) */

	int ConnectFd = -1;
	struct in_addr InAddr = {};
	struct sockaddr_in SockAddr = {};

	fd_set WSet;
	struct timeval TVal = {};

	int NReady = -1;
	int ConnectError = 0;
	socklen_t LenConnectError = sizeof ConnectError;

	InAddr.s_addr = htonl(AddrHostByteOrder);
	SockAddr.sin_family = AF_INET;
	SockAddr.sin_port = htons(PortHostByteOrder);
	SockAddr.sin_addr = InAddr;

	if (-1 == (ConnectFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0)))
		{ r = 1; goto clean; }

	while (-1 == connect(ConnectFd, (struct sockaddr *)&SockAddr, sizeof SockAddr)) {
		if (errno == EINTR)
			continue;
		if (errno == EINPROGRESS) {
			FD_ZERO(&WSet);
			FD_SET(ConnectFd, &WSet);
			TVal.tv_sec = ConnectTimeoutSec;
			TVal.tv_usec = 0;

			while (-1 == (NReady = select(ConnectFd + 1, NULL, &WSet, NULL, &TVal))) {
				if (errno == EINTR)
					continue;
				{ r = 1; goto clean; }
			}

			if (NReady == 0 || ! FD_ISSET(ConnectFd, &WSet))
				{ r = 1; goto clean; }
			if (-1 == getsockopt(ConnectFd, SOL_SOCKET, SO_ERROR, &ConnectError, &LenConnectError))
				{ r = 1; goto clean; }
			if (ConnectError != 0)
				{ r = 1; goto clean; }

			break;
		}
		{ r = 1; goto clean; }
	}

	{
		struct GsNet4DumpRemoteData Data = {};
		Data.Tripwire = GS_TRIPWIRE_NET4_DUMP_REMOTE_DATA;
		Data.Fd = ConnectFd;
		Data.IsError = 0;
		size_t SizeDump = 0;
		char OuterHeaderBuf[9] = {};
		char Header48Buf[48] = {};

		if (!!(r = gs_log_list_size_all_lowlevel(GS_LOG_LIST_GLOBAL_NAME, &SizeDump)))
			goto clean;

		if (!!(r = xs_net4_write_frame_outer_header(SizeDump, OuterHeaderBuf, sizeof OuterHeaderBuf, NULL)))
			goto clean;

		if (!!(r = aux_frame_part_write_for_payload_lowlevel(GS_FRAME_TYPE_DECL(MESSAGE_LOGDUMP), SizeDump, Header48Buf, sizeof Header48Buf)))
			goto clean;

		if (!!(r = gs_log_list_dump_all_lowlevel(GS_LOG_LIST_GLOBAL_NAME, &Data, gs_net4_dump_remote_cb)))
			goto clean;
	}

clean:
	if (ConnectFd != -1)
		close(ConnectFd);

	return r;
}

int gs_net4_crash_handler_init(
	const char *IdTokenBuf, size_t LenIdToken,
	const char *ServHostNameBuf, size_t LenServHostName,
	const char *ServPort,
	struct GsCrashHandlerDumpExtraNet4 *ioGlobal)
{
	int r = 0;

	char *Buf = NULL;
	struct addrinfo Hints = {};
	struct addrinfo *Res = NULL, *Rp = NULL;
	uint32_t AddrHostByteOrder;
	uint32_t PortHostByteOrder;

	if (!IdTokenBuf) {
		/* generate random token if missing token parameter */
		GS_ASSERT(! LenIdToken);
		LenIdToken = 20;
		if (!(Buf = (char *) malloc(LenIdToken)))
			{ r = 1; goto clean; }
		if (LenIdToken != getrandom(Buf, LenIdToken, 0))
			{ r = 1; goto clean; }
	}
	else {
		if (!(Buf = (char *) malloc(LenIdToken)))
			{ r = 1; goto clean; }
		memcpy(Buf, IdTokenBuf, LenIdToken);
	}

	Hints.ai_flags = AI_NUMERICSERV;
	// FIXME: RIP ipv6, see AF_UNSPEC
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	Hints.ai_protocol = 0;

	if (!!(r = gs_buf_ensure_haszero(ServHostNameBuf, LenServHostName + 1)))
		goto clean;

	if (!! getaddrinfo(ServHostNameBuf, ServPort, &Hints, &Res))
		{ r = 1; goto clean; }

	/* special error handling (continue) */
	for (Rp = Res; Rp != NULL; Rp = Rp->ai_next)
		if (Rp->ai_family == AF_INET || Rp->ai_addrlen == sizeof (sockaddr_in)) {
			AddrHostByteOrder = ntohl(((struct sockaddr_in *) Rp->ai_addr)->sin_addr.s_addr);
			PortHostByteOrder = ntohs(((struct sockaddr_in *) Rp->ai_addr)->sin_port);
			break;
		}
	if (Rp == NULL)
		{ r = 1; goto clean; }

	ioGlobal->mIdTokenBuf = GS_ARGOWN(&Buf);
	ioGlobal->mLenIdToken = LenIdToken;
	ioGlobal->mAddrHostByteOrder = AddrHostByteOrder;
	ioGlobal->mPortHostByteOrder = PortHostByteOrder;

clean:
	if (Res)
		freeaddrinfo(Res);
	free(Buf);

	return r;
}

int gs_net4_crash_handler_log_dump_extra_func(struct GsLogList *LogList, struct GsCrashHandlerDumpExtra *CtxBase)
{
	int r = 0;

	/* should be callable from a signal context (ex see signal-safety(7)) */
	/* use with parameters typed gs_log_list_func_dump_extra_lowlevel_t */

	struct GsCrashHandlerDumpExtraNet4 *Ctx = (struct GsCrashHandlerDumpExtraNet4 *) CtxBase;

	if (!!(r = gs_net4_crash_handler_log_dump_remote(Ctx->mIdTokenBuf, Ctx->mLenIdToken, Ctx->mAddrHostByteOrder, Ctx->mPortHostByteOrder, GS_LOG_DUMP_EXTRA_ARBITRARY_TIMEOUT_MSEC)))
		goto clean;

clean:

	return r;
}

void sender_func(struct XsServCtl *ServCtl)
{
	int r = 0;

	int ConnectFd = -1;

	const char Loopback[] = "127.0.0.1";
	const char Port[] = "3756";

	struct addrinfo Hints = {};
	struct addrinfo *Res = NULL, *Rp = NULL;

	fd_set WSet;
	struct timeval TVal = {};

	int NReady = -1;
	int ConnectError = 0;
	socklen_t LenConnectError = sizeof ConnectError;

	Hints.ai_flags = AI_NUMERICSERV;
	// FIXME: RIP ipv6, see AF_UNSPEC
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	Hints.ai_protocol = 0;

	if (!! getaddrinfo(Loopback, Port, &Hints, &Res))
		GS_ERR_CLEAN(1);

	/* special error handling (continue) */
	for (Rp = Res; Rp != NULL; Rp = Rp->ai_next)
		if (-1 != (ConnectFd = socket(Rp->ai_family, Rp->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, Rp->ai_protocol)))
			break;
	if (Rp == NULL)
		GS_ERR_CLEAN(1);

	while (-1 == connect(ConnectFd, Rp->ai_addr, Rp->ai_addrlen)) {
		if (errno == EINTR)
			continue;
		if (errno == EINPROGRESS) {
			FD_ZERO(&WSet);
			FD_SET(ConnectFd, &WSet);
			TVal.tv_sec = 5;
			TVal.tv_usec = 0;

			while (-1 == (NReady = select(ConnectFd + 1, NULL, &WSet, NULL, &TVal))) {
				if (errno == EINTR)
					continue;
				GS_ERR_CLEAN(1);
			}

			if (NReady == 0 || ! FD_ISSET(ConnectFd, &WSet))
				GS_ERR_CLEAN(1);
			if (-1 == getsockopt(ConnectFd, SOL_SOCKET, SO_ERROR, &ConnectError, &LenConnectError))
				GS_ERR_CLEAN(1);
			if (ConnectError != 0)
				GS_ERR_CLEAN(1);

			break;
		}
		GS_ERR_CLEAN(1);
	}

	while (true) {
		int nsent = 0;
		char buf[4096 + 1] = {};
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

		// FIXME: while-loop this
		nrecv = recvfrom(ConnectFd, buf, 4096, MSG_DONTWAIT, NULL, NULL);
		if (nrecv == -1 && errno != EAGAIN)
			assert(0);
		if (nrecv == -1 && errno == EAGAIN)
			buf[0] = '\0';
		printf(" s msg [%.*s]\n", nrecv, buf);

		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
	}

clean:
	if (Res)
		freeaddrinfo(Res);

	if (!!r)
		GS_ASSERT(0);
}

int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type, struct XsConExt *Ext)
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

	GS_DELETE(&Ctx, struct XsConCtx);

	return 0;
}

int cbcrank1(struct XsConCtx *CtxBase, struct GsPacket *Packet)
{
	struct XsConCtx *Ctx = (struct XsConCtx *) CtxBase;

	printf("pkt %.*s\n", (int)Packet->dataLength, Packet->data);

	Ctx->mWriteOnly.mType = XS_WRITE_ONLY_TYPE_BUFFER;
	Ctx->mWriteOnly.mData.mBuffer.mBuf = (char *)malloc(5);
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

int xs_write_only_data_buffer_init_copying(struct XsWriteOnly *WriteOnly, const char *DataBuf, size_t LenData)
{
	int r = 0;

	if (WriteOnly->mType != XS_WRITE_ONLY_TYPE_NONE)
		GS_ERR_CLEAN(1);

	if (!!(r = xs_write_only_data_buffer_init_copying2(& WriteOnly->mData.mBuffer, DataBuf, LenData, NULL, 0)))
		GS_GOTO_CLEAN();

	WriteOnly->mType = XS_WRITE_ONLY_TYPE_BUFFER;

clean:

	return r;
}

int xs_write_only_data_buffer_init_copying2(
	struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer,
	const char *Data1Buf, size_t LenData1,
	const char *Data2Buf, size_t LenData2)
{
	GS_ASSERT(! WriteOnlyDataBuffer->mBuf);
	GS_ASSERT(LenData1 != 0 || LenData2 != 0);
	if (! (WriteOnlyDataBuffer->mBuf = (char *)malloc(LenData1 + LenData2)))
		return 1;
	WriteOnlyDataBuffer->mLen = LenData1 + LenData2;
	WriteOnlyDataBuffer->mOff = 0;
	if (LenData1)
		memcpy(WriteOnlyDataBuffer->mBuf + 0, Data1Buf, LenData1);
	if (LenData2)
		memcpy(WriteOnlyDataBuffer->mBuf + LenData1, Data2Buf, LenData2);
	return 0;
}

int xs_write_only_data_buffer_init_copying_outering(
	struct XsWriteOnly *WriteOnly,
	const char *HdrPlusPayloadBuf, size_t LenHdrPlusPayload)
{
	int r = 0;

	char HdrOuterBuf[9] = {};
	size_t LenHdrOuter = 0;

	if (WriteOnly->mType != XS_WRITE_ONLY_TYPE_NONE)
		GS_ERR_CLEAN(1);

	if (!!(r = xs_net4_write_frame_outer_header(LenHdrPlusPayload, HdrOuterBuf, sizeof HdrOuterBuf, &LenHdrOuter)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_write_only_data_buffer_init_copying2(
		& WriteOnly->mData.mBuffer,
		HdrOuterBuf, LenHdrOuter,
		HdrPlusPayloadBuf, LenHdrPlusPayload)))
	{
		GS_GOTO_CLEAN();
	}

	WriteOnly->mType = XS_WRITE_ONLY_TYPE_BUFFER;

clean:

	return r;
}

int xs_write_only_data_buffer_reset(struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer)
{
	if (WriteOnlyDataBuffer->mBuf)
		free(WriteOnlyDataBuffer->mBuf);
	WriteOnlyDataBuffer->mBuf = 0;
	WriteOnlyDataBuffer->mLen = 0;
	WriteOnlyDataBuffer->mOff = 0;
	return 0;
}

int xs_write_only_data_buffer_advance(int Fd, struct XsWriteOnlyDataBuffer *Buffer)
{
	/* caller inspects Buffer->mOff == Buffer->mLen for completion testing */
	/* on success, either has completed or has reached EAGAIN */
	int r = 0;

	GS_ASSERT(Buffer->mOff <= Buffer->mLen);

	if (Buffer->mOff == Buffer->mLen)
		GS_ERR_NO_CLEAN(0);

	while (Buffer->mOff < Buffer->mLen) {
		ssize_t nsent = 0;
		while (-1 == (nsent = sendto(Fd, Buffer->mBuf + Buffer->mOff, Buffer->mLen - Buffer->mOff, MSG_NOSIGNAL, NULL, 0))) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				GS_ERR_NO_CLEAN(0);
			GS_ERR_CLEAN(1);
		}
		Buffer->mOff += nsent;
	}

noclean:

clean:

	return r;
}

int xs_write_only_data_send_file_advance(int Fd, struct XsWriteOnlyDataSendFile *SendFile)
{
	/* caller inspects SendFile->mOff == SendFile->mLen for completion */
	int r = 0;
	int nsent = 0;

	GS_ASSERT(SendFile->mOff <= SendFile->mLen);

	// FIXME: be smarter about requested count remember no such thing as nonblocking file read (linux)
	while (-1 == (nsent = sendfile(Fd, SendFile->mFd, NULL, SendFile->mLen - SendFile->mOff))) {
		if (errno == EINTR)
			continue;
		if (errno == EAGAIN)
			GS_ERR_NO_CLEAN(0);
		GS_ERR_CLEAN(1);
	}

	SendFile->mOff += nsent;

noclean:

clean:

	return r;
}

int xs_eventfd_read(int EvtFd)
{
	int r = 0;
	char    Buf[8] = {};
	ssize_t nread  = 0;

	while (-1 == (nread = read(EvtFd, Buf, 8))) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			continue;
		GS_ERR_CLEAN(1);
	}
	GS_ASSERT(nread == 8);

clean:

	return r;
}

int xs_eventfd_write(int EvtFd, int Value)
{
	int r = 0;

	uint64_t Val    = (uint64_t)Value;
	char     Buf[8] = {};
	ssize_t  nwrite = 0;

	GS_ASSERT(sizeof Val == sizeof Buf);

	memcpy(Buf, &Val, 8);

	while (-1 == (nwrite = write(EvtFd, Buf, 8))) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			continue;
		GS_ERR_CLEAN(1);
	}
	GS_ASSERT(nwrite == 8);

clean:

  return 0;
}

int xs_net4_write_frame_outer_header(
	size_t LenData,
	char *ioNineCharBuf, size_t NineCharBufSize, size_t *oLenNineCharBuf)
{
	/* signalsafe / lowlevel - must be callable in a crash handler */
	if (NineCharBufSize < 9) return 1;
	memcpy(ioNineCharBuf + 0, "FRAME", 5);
	aux_uint32_to_LE(LenData, ioNineCharBuf + 5, 4);
	*oLenNineCharBuf = 9;
	return 0;
}

int xs_con_ext_base_init(struct XsConExt *ExtBase)
{
	return 0;
}

int xs_con_ext_base_reset(struct XsConExt *ExtBase)
{
	return 0;
}

int xs_serv_ctl_destroy(struct XsServCtl *ServCtl)
{
	GS_DELETE(&ServCtl, struct XsServCtl);

	return 0;
}

int xs_serv_ctl_quit_request(struct XsServCtl *ServCtl)
{
	int r = 0;

	if (!!(r = xs_eventfd_write(ServCtl->mEvtFdExitReq, 1)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int xs_serv_ctl_quit_wait(struct XsServCtl *ServCtl)
{
	int r = 0;

	if (!!(r = xs_eventfd_read(ServCtl->mEvtFdExitReq)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_eventfd_write(ServCtl->mEvtFdExit, ServCtl->mNumThread)))
		GS_GOTO_CLEAN();

	for (size_t i = 0; i < ServCtl->mThread.size(); i++)
		ServCtl->mThread[i]->join();

clean:

	return r;
}

int crank1readnet(struct XsConCtx *Ctx)
{
	int r = 0;

	bool DoneReading = 0;

	while (! DoneReading) {
		ssize_t nrecv = 0;
		size_t SpaceLeft = Ctx->mRcvBuf.mBufSize - Ctx->mRcvBuf.mBufLen;

		GS_ASSERT(Ctx->mRcvBuf.mBufSize >= Ctx->mRcvBuf.mBufLen);

		// FIXME: only grows, never shrinks
		if (SpaceLeft < XS_RCV_BUF_SPACE_LEFT_MIN) {
			char *NewBuf = NULL;
			if (! (NewBuf = (char *)gs_realloc(Ctx->mRcvBuf.mBuf, Ctx->mRcvBuf.mBufSize * 2)))
				assert(0);
			Ctx->mRcvBuf.mBuf = NewBuf;
			Ctx->mRcvBuf.mBufSize *= 2;
			SpaceLeft = Ctx->mRcvBuf.mBufSize - Ctx->mRcvBuf.mBufLen;
		}

		while (-1 == (nrecv = recvfrom(Ctx->mFd, Ctx->mRcvBuf.mBuf + Ctx->mRcvBuf.mBufLen, SpaceLeft, MSG_DONTWAIT, NULL, NULL))) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN) {
				DoneReading = 1;
				break;
			}
			GS_ERR_CLEAN(1);
		}

		if (! DoneReading)
			Ctx->mRcvBuf.mBufLen += nrecv;
	}

clean:

	return r;
}

int crank1readframe(struct XsConCtx *Ctx)
{
	int r = 0;

	const size_t FrameHdrLen = 5 /*MAGIC*/ + 4 /*len*/;
	size_t Offset = Ctx->mRcvBuf.mBufOffset;

	/* we can process frames until we are requested writeonly mode */
	while (Ctx->mRcvBuf.mBufLen - Offset >= FrameHdrLen) {
		uint32_t FrameLen = 0;
		struct GsPacket Packet = {};

		if (!!(r = memcmp(Ctx->mRcvBuf.mBuf + Offset, "FRAME", 5)))
			GS_GOTO_CLEAN();
		Offset += 5;

		aux_LE_to_uint32(&FrameLen, Ctx->mRcvBuf.mBuf + Offset, 4);
		Offset += 4;

		if (Ctx->mRcvBuf.mBufLen - Offset < FrameLen)
			break;

		/* have frame */
		Packet.data = (uint8_t *)Ctx->mRcvBuf.mBuf + Offset;
		Packet.dataLength = FrameLen;
		/* handoff frame */
		Offset += FrameLen;
		GS_ASSERT(Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE);
		if (!!(r = Ctx->CbCrank(Ctx, &Packet)))
			GS_GOTO_CLEAN();
		if (Ctx->mWriteOnly.mType != XS_WRITE_ONLY_TYPE_NONE)
			break;
	}

	Ctx->mRcvBuf.mBufOffset = Offset;

clean:

	return r;
}

int accept_1(struct XsConCtx *OldCtx, int EPollFd)
{
	int r = 0;

	bool DoneAccepting = 0;

	while (! DoneAccepting) {
		int NewFd = -1;
		// FIXME: RIP ipv6
		struct sockaddr_in Addr = {};
		socklen_t AddrLen = sizeof Addr;

		while (-1 == (NewFd = accept4(OldCtx->mFd, (struct sockaddr *) &Addr, &AddrLen, SOCK_NONBLOCK | SOCK_CLOEXEC))) {
			int e = errno;
			if (e == EINTR)
				continue;
			/* this is not a failure of accept, but a pending error on new socket */
			/* see accept(2) (error handling section) */
			/* '.. should detect .. and treat then like EAGAIN ..' */
			// FIXME: have logging on this case
			if (e == ENETDOWN || e == EPROTO || e == ENOPROTOOPT || e == EHOSTDOWN
				|| e == ENONET || e == EHOSTUNREACH || e == EOPNOTSUPP || e == ENETUNREACH)
			{
				DoneAccepting = 1;
				break;
			}
			if (errno == EAGAIN || errno == EWOULDBLOCK) {
				DoneAccepting = 1;
				break;
			}
			GS_ERR_CLEAN(1);
		}

		if (! DoneAccepting) {
			struct XsEPollCtx *EPollCtx = NULL;
			struct XsConCtx   *Ctx = NULL;
			struct epoll_event NewEvt = {};

			EPollCtx = new XsEPollCtx();

			if (!!(r = OldCtx->CbCtxCreate(&Ctx, XS_SOCK_TYPE_NORMAL, OldCtx->mExt)))
				GS_GOTO_CLEANSUB();

			Ctx->mFd = NewFd; NewFd = -1;
			Ctx->mExt = OldCtx->mExt;
			Ctx->mRcvBuf.mBufSize = XS_RCV_BUF_STA_SIZE;
			Ctx->mRcvBuf.mBufLen = 0;
			Ctx->mRcvBuf.mBuf = (char *)malloc(XS_RCV_BUF_STA_SIZE);
			Ctx->mRcvBuf.mBufOffset = 0;
			Ctx->mWriteOnly.mType = XS_WRITE_ONLY_TYPE_NONE;

			if (! Ctx->mRcvBuf.mBuf)
				GS_ERR_CLEANSUB(1);

			EPollCtx->mType = XS_SOCK_TYPE_NORMAL;
			EPollCtx->mCtx = Ctx;

			NewEvt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
			NewEvt.data.ptr = EPollCtx;

			if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_ADD, Ctx->mFd, &NewEvt))
				GS_ERR_CLEANSUB(1);

		cleansub:
			if (!!r) {
				GS_DELETE_VF(&Ctx, CbCtxDestroy);
				GS_DELETE(&EPollCtx, struct XsEPollCtx);
				if (NewFd != -1)
					close(NewFd);
			}
			if (!!r)
				goto clean;
		}
	}

clean:

	return r;
}

int crank1(struct XsConCtx *Ctx)
{
	int r = 0;

	if (!!(r = crank1readnet(Ctx)))
		GS_GOTO_CLEAN();

	if (!!(r = crank1readframe(Ctx)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int writeonly1(struct XsConCtx *Ctx)
{
	int r = 0;

	if (!!(r = Ctx->CbWriteOnly(Ctx)))
		GS_GOTO_CLEAN();

	if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
		if (!!(r = crank1readframe(Ctx)))
			GS_GOTO_CLEAN();

clean:

	return r;
}

void receiver_func(int Id, int EPollFd, int EvtFdExitReq)
{
	int r = 0;

	log_guard_t Log(GS_LOG_GET("serv_worker"));

	while (true) {
		struct epoll_event events[NEVTS] = {};
		int nready = 0;

		while (-1 == (nready = epoll_wait(EPollFd, events, NEVTS, 100))) {
			if (errno == EINTR)
				continue;
			GS_ERR_CLEAN(1);
		}
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
					if (!!(r = accept_1(Ctx, EPollFd)))
						GS_GOTO_CLEAN();

					newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
					newevt.data.ptr = EPollCtx;

					if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt))
						GS_ERR_CLEAN(1);
				}
				break;

				case XS_SOCK_TYPE_NORMAL:
				{
					GS_ASSERT(Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE);

					if (!!(r = crank1(Ctx)))
						GS_GOTO_CLEAN();

					if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
						newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
					else
						newevt.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
					newevt.data.ptr = EPollCtx;

					if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt))
						GS_ERR_CLEAN(1);
				}
				break;

				case XS_SOCK_TYPE_EVENT:
				{
					if (!!(r = xs_eventfd_read(Ctx->mFd)))
						GS_GOTO_CLEAN();

					newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
					newevt.data.ptr = EPollCtx;

					if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt))
						assert(0);

					GS_ERR_NO_CLEAN(0);
				}
				break;

				default:
					GS_ASSERT(0);
				}
			} else if (events[i].events & EPOLLOUT) {
				GS_ASSERT(EPollCtx->mType == XS_SOCK_TYPE_NORMAL);

				GS_ASSERT(Ctx->mWriteOnly.mType != XS_WRITE_ONLY_TYPE_NONE);

				if (!!(r = writeonly1(Ctx)))
					GS_GOTO_CLEAN();

				if (Ctx->mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
					newevt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
				else
					newevt.events = EPOLLOUT | EPOLLET | EPOLLONESHOT;
				newevt.data.ptr = EPollCtx;

				if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_MOD, Ctx->mFd, &newevt))
					GS_ERR_CLEAN(1);
			}
		}
	}

noclean:

clean:
	if (!!r)
		GS_ASSERT(0);
}

int xs_net4_listenme(int ListenFd, xs_cb_ctx_create_t CbCtxCreate, struct XsConExt *Ext, struct XsServCtl **oServCtl)
{
	int r = 0;

	struct XsServCtl *ServCtl = NULL;

	std::vector<sp<std::thread> > ThreadRecv;

	size_t NumThread = 2;

	// thread requesting exit 0 -> 1 -> 0
	int EvtFdExitReq = -1;
	// controller (this) ordering exit 0 -> NumThread -> -=1 -> .. -> 0
	int EvtFdExit = -1;
	int EPollFd = -1;

	struct XsEPollCtx *ExitEPollCtx = NULL;
	struct XsConCtx   *ExitCtx = NULL;
	struct epoll_event ExitEvt = {};

	struct XsEPollCtx *SockEPollCtx = NULL;
	struct XsConCtx   *SockCtx = NULL;
	struct epoll_event SockEvt = {};

	if (-1 == (EvtFdExitReq = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE)))
		GS_ERR_CLEAN(1);

	if (-1 == (EvtFdExit = eventfd(0, EFD_CLOEXEC | EFD_SEMAPHORE)))
		GS_ERR_CLEAN(1);

	if (-1 == (EPollFd = epoll_create1(EPOLL_CLOEXEC)))
		GS_ERR_CLEAN(1);

	if (!!(r = CbCtxCreate(&ExitCtx, XS_SOCK_TYPE_EVENT, Ext)))
		GS_GOTO_CLEAN();

	ExitCtx->mFd = EvtFdExit;
	ExitCtx->mExt = Ext;
	ExitEPollCtx = new XsEPollCtx();
	ExitEPollCtx->mType = XS_SOCK_TYPE_EVENT;
	ExitEPollCtx->mCtx = ExitCtx;
	ExitEvt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	ExitEvt.data.ptr = ExitEPollCtx;

	if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_ADD, ExitCtx->mFd, &ExitEvt))
		GS_ERR_CLEAN(1);

	if (!!(r = CbCtxCreate(&SockCtx, XS_SOCK_TYPE_LISTEN, Ext)))
		GS_GOTO_CLEAN();

	SockCtx->mFd = ListenFd;
	SockCtx->mExt = Ext;
	SockEPollCtx = new XsEPollCtx();
	SockEPollCtx->mType = XS_SOCK_TYPE_LISTEN;
	SockEPollCtx->mCtx = SockCtx;
	SockEvt.events = EPOLLIN | EPOLLET | EPOLLONESHOT;
	SockEvt.data.ptr = SockEPollCtx;

	if (-1 == epoll_ctl(EPollFd, EPOLL_CTL_ADD, SockCtx->mFd, &SockEvt))
		GS_ERR_CLEAN(1);

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

clean:
	if (!!r) {
		GS_DELETE_F(&ServCtl, xs_serv_ctl_destroy);
		GS_DELETE_VF(&SockCtx, CbCtxDestroy);
		GS_DELETE(&SockEPollCtx, struct XsEPollCtx);
		GS_DELETE_VF(&ExitCtx, CbCtxDestroy);
		GS_DELETE(&ExitEPollCtx, struct XsEPollCtx);
	}

	return r;
}

int xs_net4_socket_listen_create(const char *Port, int *oListenFd)
{
	int r = 0;
	int ListenFd = -1;

	struct addrinfo Hints = {};
	struct addrinfo *Res = NULL, *Rp = NULL;

	/* AI_PASSIVE causes INADDR_ANY equivalent */
	Hints.ai_flags = AI_PASSIVE | AI_NUMERICSERV;
	// FIXME: RIP ipv6, see AF_UNSPEC
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;
	Hints.ai_protocol = 0;

	if (!!(r = getaddrinfo(NULL, Port, &Hints, &Res)))
		GS_GOTO_CLEAN();

	for (Rp = Res; Rp != NULL; Rp = Rp->ai_next) {
		if (-1 == (ListenFd = socket(Rp->ai_family, Rp->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC, Rp->ai_protocol)))
			GS_ERR_CLEAN(1);

		if (0 == bind(ListenFd, Rp->ai_addr, Rp->ai_addrlen))
			break;

		close(ListenFd);
	}
	if (Rp == NULL)
		GS_ERR_CLEAN(1);
	else
		freeaddrinfo(Res);

	if (-1 == listen(ListenFd, SOMAXCONN))
		GS_ERR_CLEAN(1);

	if (oListenFd)
		*oListenFd = ListenFd;

clean:
	if (!!r) {
		if (ListenFd != -1)
			close(ListenFd);
	}

	return r;
}

int xs_main(int argc, char **argv)
{
  int ListenFd = -1;
  struct XsServCtl *ServCtl = NULL;
  sp<std::thread> ThreadSend;

  if (!! xs_net4_socket_listen_create("3756", &ListenFd))
	  assert(0);

  if (!! xs_net4_listenme(ListenFd, cbctxcreate, NULL, &ServCtl))
    assert(0);

  ThreadSend = std::make_shared<std::thread>(sender_func, ServCtl);

  if (!! xs_serv_ctl_quit_wait(ServCtl))
    assert(0);

  xs_serv_ctl_destroy(ServCtl);

  return EXIT_SUCCESS;
}
