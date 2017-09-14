#ifdef _MSC_VER
#pragma warning(disable : 4267 4102)  // conversion from size_t, unreferenced label
#endif /* _MSC_VER */

#include <git2.h>

#define EVENT2_VISIBILITY_STATIC_MSVC
#include <event2/event.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <gittest/misc.h>
#include <gittest/bypart_git.h>
#include <gittest/config.h>
#include <gittest/log.h>
#include <gittest/frame.h>
#include <gittest/gittest.h>  // aux_LE_to_uint32
#include <gittest/gittest_ev2_test.h>

static int gs_ev_selfupdate_crank3_connected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase);
static int gs_ev_selfupdate_crank3_disconnected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	int DisconnectReason);
static int gs_ev_selfupdate_crank3(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	struct GsEvData *Packet);

int gs_selfupdate_state_code(
	struct GsSelfUpdateState *State,
	uint32_t *oCode)
{
	int r = 0;
	
	int Code = 0;

	if (! State->mRepositoryT)
		{ Code = GS_SELFUPDATE_STATE_CODE_NEED_REPOSITORY; goto need_repository; }
	if (! State->mBlobHeadOid)
		{ Code = GS_SELFUPDATE_STATE_CODE_NEED_BLOB_HEAD; goto need_blob_head; }
	if (! State->mBufferUpdate)
		{ Code = GS_SELFUPDATE_STATE_CODE_NEED_BLOB; goto need_written_blob;}
	if (true)
		{ Code = GS_SELFUPDATE_STATE_CODE_NEED_NOTHING; goto need_nothing; }

need_repository:
	if (State->mBlobHeadOid)
		GS_ERR_CLEAN(1);
need_blob_head:
	if (State->mBufferUpdate)
		GS_ERR_CLEAN(1);
need_written_blob:
need_nothing:

	if (oCode)
		*oCode = Code;

clean:

	return r;
}

int gs_selfupdate_state_code_ensure(
	struct GsSelfUpdateState *State,
	uint32_t WantedCode)
{
	int r = 0;

	uint32_t FoundCode = 0;

	if (!!(r = gs_selfupdate_state_code(State, &FoundCode)))
		GS_GOTO_CLEAN();

	if (WantedCode != FoundCode)
		GS_ERR_CLEAN(1);

clean:

	return r;
}

int gs_ev_selfupdate_crank3_connected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase)
{
	int r = 0;

	struct GsEvCtxSelfUpdate *Ctx = (struct GsEvCtxSelfUpdate *) CtxBase;

	std::string Buffer;

	git_repository *RepositoryT = NULL;
	git_repository *RepositoryMemory = NULL;

	GS_BYPART_DATA_VAR(String, BysizeBuffer);
	GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

	GS_ASSERT(Ctx->base.mMagic == GS_EV_CTX_SELFUPDATE_MAGIC);

	if (!!(r = gs_selfupdate_state_code_ensure(Ctx->mState, GS_SELFUPDATE_STATE_CODE_NEED_REPOSITORY)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_repo_init(Ctx->mCommonVars.RepoMasterUpdatePathBuf, Ctx->mCommonVars.LenRepoMasterUpdatePath, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(Ctx->mCommonVars.RepoMasterUpdatePathBuf, Ctx->mCommonVars.LenRepoMasterUpdatePath, &RepositoryT)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_memory_repository_new(&RepositoryMemory)))
		GS_GOTO_CLEAN();

	Ctx->mState->mRepositoryT = sp<git_repository *>(new git_repository *(RepositoryT));
	Ctx->mState->mRepositoryMemory = sp<git_repository *>(new git_repository *(RepositoryMemory));

	if (!!(r = gs_selfupdate_state_code_ensure(Ctx->mState, GS_SELFUPDATE_STATE_CODE_NEED_BLOB_HEAD)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_full_write_request_latest_selfupdate_blob(gs_bysize_cb_String, &BysizeBuffer)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_ev_selfupdate_crank3_disconnected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	int DisconnectReason)
{
	int r = 0;

	bufferevent_free(Bev);

	GS_LOG(I, S, "crank3 disconnected, err-trigger");

	r = 1;

	return r;
}

int gs_ev_selfupdate_crank3(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	struct GsEvData *Packet)
{
	int r = 0;

	struct GsEvCtxSelfUpdate *Ctx = (struct GsEvCtxSelfUpdate *) CtxBase;

	uint32_t Code = 0;

process_another_state_label:

	if (!!(r = gs_selfupdate_state_code(Ctx->mState, &Code)))
		GS_GOTO_CLEAN();

	switch (Code) {
	case GS_SELFUPDATE_STATE_CODE_NEED_BLOB_HEAD:
	{
		sp<git_oid> BlobHeadOid(new git_oid);

		std::string Buffer;
		uint32_t Offset = 0;

		git_oid BlobOldOidT = {};

		GsStrided BlobHeadOidStrided = {};

		GS_BYPART_DATA_VAR(String, BysizeBuffer);
		GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

		if (!!(r = gs_strided_for_oid_vec(BlobHeadOid.get(), 1, &BlobHeadOidStrided)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_LATEST_SELFUPDATE_BLOB))))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, GS_PAYLOAD_OID_LEN)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_oid(Packet->data, Packet->dataLength, Offset, &Offset, BlobHeadOid->id, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_buf_ensure_haszero(Ctx->mCurExeBuf, Ctx->mLenCurExe + 1)))
			GS_GOTO_CLEAN();

		/* empty as_path parameter means no filters applied */
		if (!!(r = git_repository_hashfile(&BlobOldOidT, *Ctx->mState->mRepositoryMemory, Ctx->mCurExeBuf, GIT_OBJ_BLOB, "")))
			GS_GOTO_CLEAN_L(E, PF, "failure hashing [filename=[%.*s]]", Ctx->mCommonVars.LenSelfUpdateExePath, Ctx->mCommonVars.SelfUpdateExePathBuf);

		if (git_oid_cmp(&BlobOldOidT, BlobHeadOid.get()) == 0) {
			char buf[GIT_OID_HEXSZ] = {};
			git_oid_fmt(buf, &BlobOldOidT);
			GS_LOG(I, PF, "have latest [oid=[%.*s]]", (int)GIT_OID_HEXSZ, buf);

			// FIXME: delegate to cleansub (nocleansub)
			GS_ERR_NO_CLEAN(GS_ERRCODE_EXIT);
		}

		Ctx->mState->mBlobHeadOid = BlobHeadOid;

		if (!!(r = gs_selfupdate_state_code_ensure(Ctx->mState, GS_SELFUPDATE_STATE_CODE_NEED_BLOB)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_request_blobs_selfupdate(BlobHeadOidStrided, gs_bysize_cb_String, &BysizeBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_SELFUPDATE_STATE_CODE_NEED_BLOB:
	{
		uint32_t Offset = 0;
		uint32_t DataLengthLimit = 0;

		uint32_t BlobPairedVecLen = 0;
		uint32_t BlobOffsetSizeBuffer = 0;
		uint32_t BlobOffsetObjectBuffer = 0;

		uint32_t  BlobZeroSize = 0;
		git_oid   BlobZeroOid = {};
		git_blob *BlobZero = NULL;

		if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_BLOBS_SELFUPDATE))))
			GS_GOTO_CLEANSUB();

		if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &DataLengthLimit)))
			GS_GOTO_CLEANSUB();

		if (!!(r = aux_frame_full_aux_read_paired_vec_noalloc(
			Packet->data, DataLengthLimit, Offset, &Offset,
			&BlobPairedVecLen, &BlobOffsetSizeBuffer, &BlobOffsetObjectBuffer)))
		{
			GS_GOTO_CLEANSUB();
		}

		if (BlobPairedVecLen != 1)
			GS_ERR_CLEANSUB(1);

		aux_LE_to_uint32(&BlobZeroSize, (char *)(Packet->data + BlobOffsetSizeBuffer), GS_FRAME_SIZE_LEN);

		if (!!(r = git_blob_create_frombuffer(&BlobZeroOid, *Ctx->mState->mRepositoryMemory, Packet->data + BlobOffsetObjectBuffer, BlobZeroSize)))
			GS_GOTO_CLEANSUB();

		if (!!(r = git_blob_lookup(&BlobZero, *Ctx->mState->mRepositoryMemory, &BlobZeroOid)))
			GS_GOTO_CLEANSUB();

		/* wtf? was the wrong blob sent? */
		if (git_oid_cmp(&BlobZeroOid, Ctx->mState->mBlobHeadOid.get()) != 0)
			GS_ERR_CLEANSUB(1);

		Ctx->mState->mBufferUpdate = sp<std::string>(new std::string((char *)git_blob_rawcontent(BlobZero), git_blob_rawsize(BlobZero)));

		if (!!(r = gs_selfupdate_state_code_ensure(Ctx->mState, GS_SELFUPDATE_STATE_CODE_NEED_NOTHING)))
			GS_GOTO_CLEANSUB();

	cleansub:
		git_blob_free(BlobZero);

		if (!!r)
			GS_GOTO_CLEAN();

		goto process_another_state_label;
	}
	break;

	case GS_SELFUPDATE_STATE_CODE_NEED_NOTHING:
	{
		GS_ERR_NO_CLEAN(GS_ERRCODE_EXIT);
	}
	break;

	default:
		GS_ASSERT(0);
	}

noclean:

clean:

	return r;
}

int gs_ev_ctx_selfupdate_destroy(struct GsEvCtxSelfUpdate *w)
{
	if (w)
		delete w;
	return 0;
}

int gs_ev2_test_selfupdatemain(
	struct GsAuxConfigCommonVars CommonVars,
	const char *CurExeBuf, size_t LenCurExe,
	struct GsEvCtxSelfUpdate **oCtx)
{
	int r = 0;

	struct GsEvCtxSelfUpdate *Ctx = new GsEvCtxSelfUpdate();

	Ctx->base.mMagic = GS_EV_CTX_SELFUPDATE_MAGIC;
	Ctx->base.mIsError = 0;
	Ctx->base.CbConnect = gs_ev_selfupdate_crank3_connected;
	Ctx->base.CbDisconnect = gs_ev_selfupdate_crank3_disconnected;
	Ctx->base.CbCrank = gs_ev_selfupdate_crank3;
	Ctx->base.CbWriteOnly = NULL;
	Ctx->mCommonVars = CommonVars;
	Ctx->mCurExeBuf = CurExeBuf; Ctx->mLenCurExe = LenCurExe;
	Ctx->mState = new GsSelfUpdateState();

	if (!!(r = gs_ev2_connect(
		&Ctx->base,
		CommonVars.ServHostNameBuf, CommonVars.LenServHostName,
		CommonVars.ServPort)))
	{
		GS_GOTO_CLEAN();
	}

	GS_LOG(I, S, "exiting");

	if (oCtx)
		*oCtx = Ctx;

clean:

	return r;
}
