#ifndef _GITTEST_NET4_H_
#define _GITTEST_NET4_H_

#include <stdint.h>

#include <gittest/misc.h>
#include <gittest/gittest_ev2_test.h>  // ex GsPacket

#define XS_CRASH_HANDLER_REMOTE_SEND_SIZE_MAX (200 * 1024)

#define XS_WRITE_ONLY_TYPE_NONE     0
#define XS_WRITE_ONLY_TYPE_BUFFER   1
#define XS_WRITE_ONLY_TYPE_SENDFILE 2
#define XS_WRITE_ONLY_TYPE_MAX      0x7FFFFFFF

typedef unsigned long XsWriteOnlyType;

/* intended to be forward-declared in header (API use pointer only) */
struct XsServCtl;

struct GsCrashHandlerDumpExtraNet4
{
	struct GsCrashHandlerDumpExtra base;

	const char *mIdTokenBuf; size_t mLenIdToken;
	uint32_t mAddrHostByteOrder; uint32_t mPortHostByteOrder;
};

enum XsSockType
{
	XS_SOCK_TYPE_LISTEN = 1,
	XS_SOCK_TYPE_NORMAL = 2,
	XS_SOCK_TYPE_EVENT = 3,
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
	XsWriteOnlyType mType;
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

struct XsConExt
{
};

struct XsConCtx
{
	int mFd;
	struct XsConExt *mExt; /*notowned*/
	struct XsWriteOnly mWriteOnly;
	struct XsRcvBuf mRcvBuf;
	/* must set the callbacks - caller inits other members
	likely want an extra context parameter for communication */
	int(*CbCtxCreate)(struct XsConCtx **oCtxBase, enum XsSockType Type, struct XsConExt *Ext);
	int(*CbCtxDestroy)(struct XsConCtx *CtxBase);
	int(*CbCrank)(struct XsConCtx *CtxBase, struct GsPacket *Packet);
	int(*CbWriteOnly)(struct XsConCtx *CtxBase);
};

typedef int(*xs_cb_ctx_create_t)(struct XsConCtx **oCtxBase, enum XsSockType Type, struct XsConExt *Ext);

int gs_net4_crash_handler_log_dump_remote(
	const char *IdTokenBuf, size_t LenIdToken,
	uint32_t AddrHostByteOrder, uint32_t PortHostByteOrder,
	size_t ConnectTimeoutSec);
int gs_net4_crash_handler_init(
	const char *IdTokenBuf, size_t LenIdToken,
	const char *ServHostNameBuf, size_t LenServHostName,
	const char *ServPort,
	struct GsCrashHandlerDumpExtraNet4 *ioGlobal);
int gs_net4_crash_handler_log_dump_extra_func(struct GsLogList *LogList, struct GsCrashHandlerDumpExtra *CtxBase);

int xs_net4_write_frame_outer_header(
	size_t LenData,
	char *ioNineCharBuf, size_t NineCharBufSize, size_t *oLenNineCharBuf);

int xs_con_ext_base_init(struct XsConExt *ExtBase);
int xs_con_ext_base_reset(struct XsConExt *ExtBase);

int xs_serv_ctl_destroy(struct XsServCtl *ServCtl);
int xs_serv_ctl_quit_request(struct XsServCtl *ServCtl);
int xs_serv_ctl_quit_wait(struct XsServCtl *ServCtl);

int xs_write_only_data_buffer_init_copying(struct XsWriteOnly *WriteOnly, const char *DataBuf, size_t LenData);
int xs_write_only_data_buffer_init_copying2(
	struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer,
	const char *Data1Buf, size_t LenData1,
	const char *Data2Buf, size_t LenData2);
int xs_write_only_data_buffer_init_copying_outering(
	struct XsWriteOnly *WriteOnly,
	const char *HdrPlusPayloadBuf, size_t LenHdrPlusPayload);
int xs_write_only_data_buffer_reset(struct XsWriteOnlyDataBuffer *WriteOnlyDataBuffer);

int xs_write_only_data_buffer_advance(int Fd, struct XsWriteOnlyDataBuffer *Buffer);
int xs_write_only_data_send_file_advance(int Fd, struct XsWriteOnlyDataSendFile *SendFile);

int xs_net4_listenme(int ListenFd, xs_cb_ctx_create_t CbCtxCreate, struct XsConExt *Ext, struct XsServCtl **oServCtl);
int xs_net4_socket_listen_create(const char *Port, int *oListenFd);

int gs_net4_serv_start(struct GsAuxConfigCommonVars *CommonVars);

#endif /* _GITTEST_NET4_H_ */
