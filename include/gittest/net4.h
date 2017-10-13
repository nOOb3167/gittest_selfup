#ifndef _GITTEST_NET4_H_
#define _GITTEST_NET4_H_

#include <gittest/misc.h>
#include <gittest/gittest_ev2_test.h>  // ex GsPacket

/* intended to be forward-declared in header (API use pointer only) */
struct XsServCtl;

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
	XS_WRITE_ONLY_MAX = 0x7FFFFFFF,
};

struct XsWriteOnlyDataBuffer
{
	char       *mBuf;
	size_t      mLen;
	size_t      mOff;
};

// platform ifdefable
struct XsWriteOnlyDataSendFile
{
	int mFd;
	size_t mLen;
	size_t mOff;
};

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
	int(*CbCtxCreate)(struct XsConCtx **oCtxBase, enum XsSockType Type);
	int(*CbCtxDestroy)(struct XsConCtx *CtxBase);
	int(*CbCrank)(struct XsConCtx *CtxBase, struct GsPacket *Packet);
	int(*CbWriteOnly)(struct XsConCtx *CtxBase);
};

typedef int(*xs_cb_ctx_create_t)(struct XsConCtx **oCtxBase, enum XsSockType Type);

int xs_net4_write_frame_outer_header(
	size_t LenData,
	char *ioNineCharBuf, size_t NineCharBufSize, size_t *oLenNineCharBuf);

int xs_serv_ctl_destroy(struct XsServCtl *ServCtl);
int xs_serv_ctl_quit_request(struct XsServCtl *ServCtl);
int xs_serv_ctl_quit_wait(struct XsServCtl *ServCtl);

int xs_write_only_data_buffer_init_copying(struct XsWriteOnly *WriteOnly, const char *DataBuf, size_t LenData);
int xs_write_only_data_buffer_init_copying2(
	struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer,
	const char *Data1Buf, size_t LenData1,
	const char *Data2Buf, size_t LenData2);
int xs_write_only_data_buffer_reset(struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer);

int xs_write_only_data_buffer_advance(int Fd, struct XsWriteOnlyDataBuffer *Buffer);
int xs_write_only_data_send_file_advance(int Fd, struct XsWriteOnlyDataSendFile *SendFile);

int xs_net4_listenme(int ListenFd, xs_cb_ctx_create_t CbCtxCreate, struct XsServCtl **oServCtl);
int xs_net4_socket_listen_create(const char *Port, int *oListenFd);

#endif /* _GITTEST_NET4_H_ */
