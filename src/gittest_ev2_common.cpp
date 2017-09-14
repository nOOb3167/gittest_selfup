#ifdef _MSC_VER
#pragma warning(disable : 4267 4102)  // conversion from size_t, unreferenced label
#endif /* _MSC_VER */

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <sstream>

#define EVENT2_VISIBILITY_STATIC_MSVC
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/gittest.h>
#include <gittest/gittest_ev2_test.h>
#include <gittest/frame.h>

int gs_packet_with_offset_get_veclen(
	struct GsPacketWithOffset *PacketWithOffset,
	uint32_t *oVecLen)
{
	GS_ASSERT(GS_FRAME_SIZE_LEN == sizeof(uint32_t));
	if (PacketWithOffset->mOffsetObject < PacketWithOffset->mOffsetSize ||
		(PacketWithOffset->mOffsetObject - PacketWithOffset->mOffsetSize) % GS_FRAME_SIZE_LEN != 0)
		return 1;
	if (oVecLen)
		*oVecLen = (PacketWithOffset->mOffsetObject - PacketWithOffset->mOffsetSize) / GS_FRAME_SIZE_LEN;
	return 0;
}

int gs_ev_evbuffer_get_frame_try(
	struct evbuffer *Ev,
	const char **oDataOpt,
	size_t *oLenHdr,
	size_t *oLenDataOpt)
{
	int r = 0;

	const char Magic[] = "FRAME";
	size_t LenMagic = sizeof Magic - 1;

	const char *DataOpt = NULL;
	size_t LenHdr = 0;
	size_t LenData = 0;

	const char *DataH = (const char*) evbuffer_pullup(Ev, LenMagic + sizeof(uint32_t));
	if (DataH) {
		uint32_t FrameDataLen = 0;
		if (memcmp(Magic, DataH, LenMagic) != 0)
			GS_ERR_CLEAN(1);
		aux_LE_to_uint32(&FrameDataLen, DataH + LenMagic, sizeof(uint32_t));
		const char *DataF = (const char *) evbuffer_pullup(Ev, LenMagic + sizeof(uint32_t) + FrameDataLen);
		if (DataF) {
			DataOpt = DataF + LenMagic + sizeof(uint32_t);
			LenHdr = LenMagic + sizeof(uint32_t);
			LenData = FrameDataLen;
		}
	}

clean:

	if (oDataOpt)
		*oDataOpt = DataOpt;
	if (oLenHdr)
		*oLenHdr = LenHdr;
	if (oLenDataOpt)
		*oLenDataOpt = LenData;

	return r;
}

int gs_ev_evbuffer_write_frame(
	struct evbuffer *Ev,
	const char *Data,
	size_t LenData)
{
	int r = 0;

	const char Magic[] = "FRAME";
	size_t LenMagic = sizeof Magic - 1;

	char cFrameDataLen[sizeof(uint32_t)];

	aux_uint32_to_LE(LenData, cFrameDataLen, sizeof cFrameDataLen);

	if (!!(r = evbuffer_expand(Ev, LenMagic + sizeof cFrameDataLen + LenData)))
		GS_GOTO_CLEAN();

	if (!!(r = evbuffer_add(Ev, Magic, LenMagic)))
		GS_GOTO_CLEAN();

	if (!!(r = evbuffer_add(Ev, cFrameDataLen, sizeof cFrameDataLen)))
		GS_GOTO_CLEAN();

	if (!!(r = evbuffer_add(Ev, Data, LenData)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_bev_read_aux(struct bufferevent *Bev, struct GsEvCtx *CtxBase)
{
	int r = 0;

	const char *Data = NULL;
	size_t LenHdr, LenData;

	while (true) {
		/* write callback designates 'dedicated write mode' on this bufferevent - in that case avoid crank */
		if (bev_has_cb_write(Bev))
			GS_ERR_NO_CLEAN(0);

		if (!!(r = gs_ev_evbuffer_get_frame_try(bufferevent_get_input(Bev), &Data, &LenHdr, &LenData)))
			GS_GOTO_CLEAN();

		if (! Data)
			break;

		struct GsEvData Packet = { (uint8_t *) Data, LenData };

		r = CtxBase->CbCrank(Bev, CtxBase, &Packet);
		if (!!r && r != GS_ERRCODE_EXIT)
			GS_GOTO_CLEAN();
		if (r == GS_ERRCODE_EXIT)
			GS_ERR_NO_CLEAN(r);		/* marshall GS_ERRCODE_EXIT out of this function */

		if (!!(r = evbuffer_drain(bufferevent_get_input(Bev), LenHdr + LenData)))
			GS_GOTO_CLEAN();
	}

noclean:

clean:

	return r;
}

bool bev_has_cb_write(struct bufferevent *Bev)
{
	bufferevent_data_cb Cb;
	bufferevent_getcb(Bev, NULL, &Cb, NULL, NULL);
	return !!Cb;
}

void bev_raise_cb_write(struct bufferevent *Bev)
{
	bufferevent_data_cb CbR;
	bufferevent_data_cb CbW;
	bufferevent_event_cb CbE;
	void *CbA;
	bufferevent_getcb(Bev, &CbR, &CbW, &CbE, &CbA);
	GS_ASSERT(! CbW);
	bufferevent_setcb(Bev, CbR, bev_write_cb, CbE, CbA);
}

void bev_lower_cb_write(struct bufferevent *Bev)
{
	bufferevent_data_cb CbR;
	bufferevent_data_cb CbW;
	bufferevent_event_cb CbE;
	void *CbA;
	bufferevent_getcb(Bev, &CbR, &CbW, &CbE, &CbA);
	GS_ASSERT(!! CbW);
	bufferevent_setcb(Bev, CbR, NULL, CbE, CbA);
}

int bev_lower_cb_write_ex(struct bufferevent *Bev, struct GsEvCtx *CtxBase)
{
	int r = 0;

	bev_lower_cb_write(Bev);

	if (!!(r = gs_bev_read_aux(Bev, CtxBase)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

void bev_event_cb(struct bufferevent *Bev, short What, void *CtxBaseV)
{
	int r = 0;

	struct GsEvCtx *CtxBase = (struct GsEvCtx *) CtxBaseV;

	int DisconnectReason = 0;

	if (What & BEV_EVENT_CONNECTED) {
		if (!!(r = CtxBase->CbConnect(Bev, CtxBase)))
			GS_GOTO_CLEAN();
	}
	else {
		if (What & BEV_EVENT_EOF)
			DisconnectReason = GS_DISCONNECT_REASON_EOF;
		else if (What & BEV_EVENT_TIMEOUT)
			DisconnectReason = GS_DISCONNECT_REASON_TIMEOUT;
		else if (What & BEV_EVENT_ERROR)
			DisconnectReason = GS_DISCONNECT_REASON_ERROR;

		GS_LOG(I, PF, "[beverr=[%s]]", evutil_socket_error_to_string(EVUTIL_SOCKET_ERROR()));

		if (!!(r = CtxBase->CbDisconnect(Bev, CtxBase, DisconnectReason)))
			GS_GOTO_CLEAN();
	}
	
clean:
	if (!!r) {
		CtxBase->mIsError = 1;

		if (!!(event_base_loopbreak(bufferevent_get_base(Bev))))
			GS_ASSERT(0);
	}
}

void bev_read_cb(struct bufferevent *Bev, void *CtxBaseV)
{
	int r = 0;
	struct GsEvCtx *CtxBase = (struct GsEvCtx *) CtxBaseV;

	r = gs_bev_read_aux(Bev, CtxBase);
	if (!!r && r != GS_ERRCODE_EXIT)
		GS_GOTO_CLEAN();
	if (r == GS_ERRCODE_EXIT)
		if (!!(r = event_base_loopbreak(bufferevent_get_base(Bev))))
			GS_GOTO_CLEAN();

clean:
	if (!!r) {
		CtxBase->mIsError = 1;

		if (!!(event_base_loopbreak(bufferevent_get_base(Bev))))
			GS_ASSERT(0);
	}
}

void bev_write_cb(struct bufferevent *Bev, void *CtxBaseV)
{
	int r = 0;
	struct GsEvCtx *CtxBase = (struct GsEvCtx *) CtxBaseV;

	GS_ASSERT(bev_has_cb_write(Bev));

	if (!!(r = CtxBase->CbWriteOnly(Bev, CtxBase)))
		GS_GOTO_CLEAN();

clean:
	if (!!r) {
		CtxBase->mIsError = 1;

		if (!!(event_base_loopbreak(bufferevent_get_base(Bev))))
			GS_ASSERT(0);
	}
}

void evc_listener_cb(struct evconnlistener *Listener, evutil_socket_t Fd, struct sockaddr *Addr, int AddrLen, void *CtxBaseV)
{
	int r = 0;

	struct GsEvCtx *CtxBase = (struct GsEvCtx *) CtxBaseV;

	struct event_base *Base = evconnlistener_get_base(Listener);
	struct bufferevent *Bev = NULL;
	struct timeval Timeout = {};
	Timeout.tv_sec = GS_EV_TIMEOUT_SEC;
	Timeout.tv_usec = 0;

	if (!(Bev = bufferevent_socket_new(Base, Fd, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS)))
		GS_ERR_CLEAN(1);

	bufferevent_setcb(Bev, bev_read_cb, NULL, bev_event_cb, CtxBase);

	if (!!(r = bufferevent_set_timeouts(Bev, &Timeout, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = bufferevent_enable(Bev, EV_READ)))
		GS_GOTO_CLEAN();

clean:
	if (!!r) {
		CtxBase->mIsError = 1;
		
		if (!!(event_base_loopbreak(Base)))
			GS_ASSERT(0);
	}
}

void evc_error_cb(struct evconnlistener *Listener, void *CtxBaseV)
{
	struct event_base *Base = evconnlistener_get_base(Listener);

	struct GsEvCtx *CtxBase = (struct GsEvCtx *) CtxBaseV;

	GS_LOG(E, S, "Listener failure");

	CtxBase->mIsError = 1;

	if (!!(event_base_loopbreak(Base)))
		GS_ASSERT(0);
}

int gs_ev2_listen(
	struct GsEvCtx *CtxBase,
	uint32_t ServPortU32)
{
	int r = 0;

	struct event_base *Base = NULL;

	struct addrinfo Hints = {};
	struct addrinfo *ServInfo = NULL;
	struct sockaddr *ServAddr = NULL;

	struct evconnlistener *Listener = NULL;

	std::stringstream ss;
	ss << ServPortU32;
	std::string cServPort = ss.str();

	if (!(Base = event_base_new()))
		GS_ERR_CLEAN(1);

	Hints.ai_flags = AI_PASSIVE; /* for NULL nodename in getaddrinfo */
	Hints.ai_family = AF_INET;
	Hints.ai_socktype = SOCK_STREAM;

	if (!!(r = getaddrinfo(NULL, cServPort.c_str(), &Hints, &ServInfo)))
		GS_GOTO_CLEAN();

	if (!(Listener = evconnlistener_new_bind(
		Base,
		evc_listener_cb,
		CtxBase,
		LEV_OPT_CLOSE_ON_FREE | LEV_OPT_CLOSE_ON_EXEC | LEV_OPT_REUSEABLE,
		-1,
		ServInfo->ai_addr,
		ServInfo->ai_addrlen)))
	{
		GS_ERR_CLEAN(1);
	}
	evconnlistener_set_error_cb(Listener, evc_error_cb);

	if (!!(r = event_base_loop(Base, EVLOOP_NO_EXIT_ON_EMPTY)))
		GS_GOTO_CLEAN();

	if (CtxBase->mIsError)
		GS_ERR_CLEAN(1);

clean:
	freeaddrinfo(ServInfo);

	return r;
}

int gs_ev2_connect(
	struct GsEvCtx *CtxBase,
	const char *ConnectHostNameBuf, size_t LenConnectHostName,
	uint32_t ConnectPort)
{
	int r = 0;

	struct event_base *Base = NULL;
	struct bufferevent *Bev = NULL;

	if (!!(r = gs_buf_ensure_haszero(ConnectHostNameBuf, LenConnectHostName + 1)))
		GS_GOTO_CLEAN();

	if (!(Base = event_base_new()))
		GS_GOTO_CLEAN();

	if (!(Bev = bufferevent_socket_new(Base, -1, BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS)))
		GS_GOTO_CLEAN();

	bufferevent_setcb(Bev, bev_read_cb, NULL, bev_event_cb, CtxBase);

	if (!!(r = bufferevent_enable(Bev, EV_READ)))
		GS_GOTO_CLEAN();

	if (!!(r = bufferevent_socket_connect_hostname(Bev, NULL, AF_INET, ConnectHostNameBuf, ConnectPort)))
		GS_GOTO_CLEAN();

	if (!!(r = event_base_loop(Base, EVLOOP_NO_EXIT_ON_EMPTY)))
		GS_GOTO_CLEAN();

	if (CtxBase->mIsError)
		GS_GOTO_CLEAN();

clean:

	return r;
}
