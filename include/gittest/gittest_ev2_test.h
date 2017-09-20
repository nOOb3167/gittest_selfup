#ifndef _GITTEST_EV2_TEST_H_
#define _GITTEST_EV2_TEST_H_

#include <stdint.h>

#define EVENT2_VISIBILITY_STATIC_MSVC
#include <event2/bufferevent.h>

#include <gittest/config.h>

#define GS_SELFUPDATE_ARG_MAINONLY "--xmainonly"
#define GS_SELFUPDATE_ARG_CHILD "--xchild"
#define GS_SELFUPDATE_ARG_VERSUB "--xversub"

#define GS_STR_PARENT_EXTRA_SUFFIX "_helper"
#define GS_STR_PARENT_EXTRA_SUFFIX_OLD "_helper_old"

#define GS_EV_TIMEOUT_SEC (30 * 20)

#define GS_DISCONNECT_REASON_ERROR BEV_EVENT_ERROR
#define GS_DISCONNECT_REASON_EOF BEV_EVENT_EOF
#define GS_DISCONNECT_REASON_TIMEOUT BEV_EVENT_TIMEOUT

#define GS_EV_CTX_CLNT_MAGIC 0x4E8BF2AD
#define GS_EV_CTX_SERV_MAGIC 0x4E8BF2AE
#define GS_EV_CTX_SELFUPDATE_MAGIC 0x4E8BF2AF

struct GsPacket
{
	uint8_t *data;
	size_t   dataLength;
};

/** manual-init struct
    value struct

	@sa
	   ::gs_packet_with_offset_get_veclen
*/
struct GsPacketWithOffset
{
	struct GsPacket *mPacket;
	uint32_t  mOffsetSize;
	uint32_t  mOffsetObject;
};

struct GsEvData
{
	uint8_t *data;
	size_t   dataLength;
};

struct GsEvCtx
{
	uint32_t mMagic;

	int mIsError;

	int (*CbConnect)(
		struct bufferevent *Bev,
		struct GsEvCtx *CtxBase);
	int (*CbDisconnect)(
		struct bufferevent *Bev,
		struct GsEvCtx *CtxBase,
		int DisconnectReason);
	int (*CbCrank)(
		struct bufferevent *Bev,
		struct GsEvCtx *CtxBase,
		struct GsEvData *Packet);
	int (*CbWriteOnly)(
		struct bufferevent *Bev,
		struct GsEvCtx *CtxBase);
};

enum gs_selfupdate_state_code_t {
	GS_SELFUPDATE_STATE_CODE_NEED_REPOSITORY = 0,
	GS_SELFUPDATE_STATE_CODE_NEED_BLOB_HEAD = 1,
	GS_SELFUPDATE_STATE_CODE_NEED_BLOB = 2,
	GS_SELFUPDATE_STATE_CODE_NEED_NOTHING = 3,
	GS_SELFUPDATE_STATE_CODE_MAX_ENUM = 0x7FFFFFFF,
};

struct GsSelfUpdateState
{
	sp<git_repository *> mRepositoryT;
	sp<git_repository *> mRepositoryMemory;
	sp<git_oid>          mBlobHeadOid;
	sp<std::string>      mBufferUpdate;
};

struct GsEvCtxSelfUpdate
{
	struct GsEvCtx base;
	struct GsAuxConfigCommonVars mCommonVars;
	const char *mCurExeBuf; size_t mLenCurExe;
	struct GsSelfUpdateState *mState;
};

int gs_ev_ctx_clnt_destroy(struct GsEvCtxClnt *w);
int gs_ev_ctx_selfupdate_destroy(struct GsEvCtxSelfUpdate *w);

int gs_selfupdate_state_code(
	struct GsSelfUpdateState *State,
	uint32_t *oCode);
int gs_selfupdate_state_code_ensure(
	struct GsSelfUpdateState *State,
	uint32_t WantedCode);

int gs_ev2_test_clntmain(
	struct GsAuxConfigCommonVars CommonVars,
	struct GsEvCtxClnt **oCtx);
int gs_ev2_test_servmain(struct GsAuxConfigCommonVars CommonVars);
int gs_ev2_test_selfupdatemain(
	struct GsAuxConfigCommonVars CommonVars,
	const char *CurExeBuf, size_t LenCurExe,
	struct GsEvCtxSelfUpdate **oCtx);

/* common */

int gs_packet_with_offset_get_veclen(
	struct GsPacketWithOffset *PacketWithOffset,
	uint32_t *oVecLen);

int gs_ev_evbuffer_get_frame_try(
	struct evbuffer *Ev,
	const char **oDataOpt,
	size_t *oLenHdr,
	size_t *oLenDataOpt);
int gs_ev_evbuffer_write_frame(
	struct evbuffer *Ev,
	const char *Data,
	size_t LenData);
int gs_ev_evbuffer_write_frame_outer_header(
	struct evbuffer *Ev,
	size_t LenData);

int gs_bev_read_aux(struct bufferevent *Bev, struct GsEvCtx *CtxBase);

bool bev_has_cb_write(struct bufferevent *Bev);
int bev_raise_cb_write(struct bufferevent *Bev);
int bev_lower_cb_write(struct bufferevent *Bev);
int bev_lower_cb_write_ex(struct bufferevent *Bev, struct GsEvCtx *CtxBase);

void bev_event_cb(struct bufferevent *Bev, short What, void *CtxBaseV);
void bev_read_cb(struct bufferevent *Bev, void *CtxBaseV);
void bev_write_cb(struct bufferevent *Bev, void *CtxBaseV);

void evc_listener_cb(struct evconnlistener *Listener, evutil_socket_t Fd, struct sockaddr *Addr, int AddrLen, void *CtxBaseV);
void evc_error_cb(struct evconnlistener *Listener, void *CtxBaseV);

int gs_ev2_listen(
	struct GsEvCtx *CtxBase,
	uint32_t ServPortU32);
int gs_ev2_connect(
struct GsEvCtx *CtxBase,
	const char *ConnectHostNameBuf, size_t LenConnectHostName,
	uint32_t ConnectPort);
#endif /* _GITTEST_EV2_TEST_H_ */
