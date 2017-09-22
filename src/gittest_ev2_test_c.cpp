#ifdef _MSC_VER
#pragma warning(disable : 4267 4102)  // conversion from size_t, unreferenced label
#endif /* _MSC_VER */

#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sstream>

#include <thread>
#include <chrono>

#include <git2.h>
#include <git2/sys/memes.h>
#include <git2/buffer.h>

#define EVENT2_VISIBILITY_STATIC_MSVC
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>

#include <gittest/misc.h>
#include <gittest/bypart_git.h>
#include <gittest/log.h>
#include <gittest/config.h>
#include <gittest/gittest.h>
#include <gittest/frame.h>

#include <gittest/gittest_ev2_test.h>
#include <gittest/gittest_ev2_test_c.h>

static int gs_ev_clnt_state_received_oneshot_blob_cond(
	struct bufferevent *Bev,
	struct GsEvCtxClnt *Ctx,
	struct GsEvData *Packet,
	int *oIsHandledBy);

static int gs_ev_clnt_state_crank3_connected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase);
static int gs_ev_clnt_state_crank3_disconnected(
	struct bufferevent *Bev,
	struct GsEvCtx *Ctx,
	int DisconnectReason);
static int gs_ev_clnt_state_crank3(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	struct GsEvData *Packet);

int gs_ev_clnt_state_received_oneshot_blob_cond(
	struct bufferevent *Bev,
	struct GsEvCtxClnt *Ctx,
	struct GsEvData *Packet,
	int *oIsHandledBy)
{
	int r = 0;

	int IsHandledBy = 0;

	uint32_t Offset = 0;
	uint32_t LengthLimit = 0;

	git_oid OidReceivedHdr = {};
	git_oid OidReceivedDat = {};
	git_buf Inflated = {};
	git_otype Type = GIT_OBJ_BAD;
	size_t BlobOffset;
	size_t BlobSize;

	if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_BLOBS3))))
		GS_ERR_NO_CLEAN(0);

	IsHandledBy = 1;

	GS_LOG(I, S, "receiving oneshot blob");

	if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
		GS_GOTO_CLEAN();

	GS_ASSERT(LengthLimit == Packet->dataLength);

	if (!!(r = aux_frame_read_oid(Packet->data, Packet->dataLength, Offset, &Offset, OidReceivedHdr.id, GIT_OID_RAWSZ)))
		GS_GOTO_CLEAN();

	if (!!(r = git_memes_deflate((const char *) Packet->data + Offset, LengthLimit - Offset, &Inflated, &Type, &BlobOffset, &BlobSize)))
		GS_GOTO_CLEAN();

	if (Type != GIT_OBJ_BLOB)
		GS_ERR_CLEAN(1);

	if (!!(r = git_odb_hash(&OidReceivedDat, Inflated.ptr + BlobOffset, BlobSize, Type)))
		GS_GOTO_CLEAN();

	if (!!(r = git_oid_cmp(&OidReceivedHdr, &OidReceivedDat)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_deserialize_object(* Ctx->mClntState->mRepositoryT, Inflated.ptr + BlobOffset, BlobSize, Type, &OidReceivedHdr)))
		GS_GOTO_CLEAN();

	Ctx->mClntState->mReceivedOneShotBlob->push_back(OidReceivedHdr);

noclean:
	if (oIsHandledBy)
		*oIsHandledBy = IsHandledBy;

clean:

	return r;
}

int gs_ev_clnt_state_crank3_connected(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase)
{
	int r = 0;

	struct GsEvCtxClnt *Ctx = (struct GsEvCtxClnt *) CtxBase;

	std::string Buffer;

	git_repository *RepositoryT = NULL;

	GS_BYPART_DATA_VAR(String, BysizeBuffer);
	GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

	GS_ASSERT(Ctx->base.mMagic == GS_EV_CTX_CLNT_MAGIC);

	if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_REPOSITORY)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_repo_init(Ctx->mCommonVars.RepoMasterUpdatePathBuf, Ctx->mCommonVars.LenRepoMasterUpdatePath, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(Ctx->mCommonVars.RepoMasterUpdatePathBuf, Ctx->mCommonVars.LenRepoMasterUpdatePath, &RepositoryT)))
		GS_GOTO_CLEAN();

	Ctx->mClntState->mRepositoryT = sp<git_repository *>(new git_repository *(RepositoryT));

	if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_TREE_HEAD)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_full_write_request_latest_commit_tree(gs_bysize_cb_String, &BysizeBuffer)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_ev_clnt_state_crank3_disconnected(
	struct bufferevent *Bev,
	struct GsEvCtx *Ctx,
	int DisconnectReason)
{
	int r = 0;

	bufferevent_free(Bev);

	GS_LOG(I, S, "crank3 disconnected, err-trigger");

	r = 1;

	return r;
}

int gs_ev_clnt_state_crank3(
	struct bufferevent *Bev,
	struct GsEvCtx *CtxBase,
	struct GsEvData *Packet)
{
	int r = 0;

	struct GsEvCtxClnt *Ctx = (struct GsEvCtxClnt *) CtxBase;

	uint32_t Code = 0;

	int IsHandledBy = 0;

	if (!!(r = gs_ev_clnt_state_received_oneshot_blob_cond(Bev, Ctx, Packet, &IsHandledBy)))
		GS_GOTO_CLEAN();

	if (IsHandledBy)
		GS_ERR_NO_CLEAN(0);

process_another_state_label:

	if (!!(r = clnt_state_code(Ctx->mClntState, &Code)))
		GS_GOTO_CLEAN();

	switch (Code) {
	case GS_CLNT_STATE_CODE_NEED_TREE_HEAD:
	{
		sp<git_oid> TreeHeadOid(new git_oid);

		std::string Buffer;
		uint32_t Offset = 0;

		git_oid CommitHeadOidT = {};
		git_oid TreeHeadOidT = {};

		GS_BYPART_DATA_VAR(String, BysizeBuffer);
		GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

		if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_LATEST_COMMIT_TREE))))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, GS_PAYLOAD_OID_LEN)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_oid(Packet->data, Packet->dataLength, Offset, &Offset, TreeHeadOid->id, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();

		if (!!(r = clnt_latest_commit_tree_oid(*Ctx->mClntState->mRepositoryT, Ctx->mCommonVars.RefNameMainBuf, &CommitHeadOidT, &TreeHeadOidT)))
			GS_GOTO_CLEAN();

		if (git_oid_cmp(&TreeHeadOidT, TreeHeadOid.get()) == 0) {
			char buf[GIT_OID_HEXSZ] = {};
			git_oid_fmt(buf, &CommitHeadOidT);
			GS_LOG(I, PF, "Have latest [%.*s]\n", (int)GIT_OID_HEXSZ, buf);

			// FIXME: delegate to cleansub (nocleansub)
			GS_ERR_NO_CLEAN(GS_ERRCODE_EXIT);
		}

		Ctx->mClntState->mTreeHeadOid = TreeHeadOid;

		if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_TREELIST)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_request_treelist(TreeHeadOid->id, GIT_OID_RAWSZ, gs_bysize_cb_String, &BysizeBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_CLNT_STATE_CODE_NEED_TREELIST:
	{
		sp<std::vector<git_oid> > Treelist(new std::vector<git_oid>);
		sp<std::vector<git_oid> > MissingTreelist(new std::vector<git_oid>);

		struct GsStrided MissingTreelistStrided = {};

		std::string Buffer;
		uint32_t Offset = 0;
		uint32_t LengthLimit = 0;

		GS_BYPART_DATA_VAR(String, BysizeBuffer);
		GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

		GS_BYPART_DATA_VAR(OidVector, BypartTreelist);
		GS_BYPART_DATA_INIT(OidVector, BypartTreelist, Treelist.get());

		if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_TREELIST))))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_oid_vec(Packet->data, LengthLimit, Offset, &Offset, &BypartTreelist, gs_bypart_cb_OidVector)))
			GS_GOTO_CLEAN();

		if (!!(r = clnt_missing_trees(*Ctx->mClntState->mRepositoryT, Treelist.get(), MissingTreelist.get())))
			GS_GOTO_CLEAN();

		Ctx->mClntState->mTreelist = Treelist;
		Ctx->mClntState->mMissingTreelist = MissingTreelist;

		if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_BLOBLIST)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_strided_for_oid_vec_cpp(MissingTreelist.get(), &MissingTreelistStrided)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_request_trees(MissingTreelistStrided, gs_bysize_cb_String, &BysizeBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_CLNT_STATE_CODE_NEED_BLOBLIST:
	{
		sp<std::vector<git_oid> > MissingBloblist(new std::vector<git_oid>);
		sp<GsPacketWithOffset> PacketTreeWO(new GsPacketWithOffset);

		PacketTreeWO->mPacket = new GsPacket();
		PacketTreeWO->mPacket->data = new uint8_t[Packet->dataLength];
		PacketTreeWO->mPacket->dataLength = Packet->dataLength;
		memmove(PacketTreeWO->mPacket->data, Packet->data, Packet->dataLength);

		struct GsStrided MissingBloblistStrided = {};

		std::string Buffer;
		uint32_t Offset = 0;
		uint32_t LengthLimit = 0;

		uint32_t BufferTreeLen = 0;

		GS_BYPART_DATA_VAR(String, BysizeBuffer);
		GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

		if (!!(r = aux_frame_ensure_frametype(PacketTreeWO->mPacket->data, PacketTreeWO->mPacket->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_TREES))))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_size_limit(PacketTreeWO->mPacket->data, PacketTreeWO->mPacket->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_aux_read_paired_vec_noalloc(PacketTreeWO->mPacket->data, LengthLimit, Offset, &Offset,
			NULL, &PacketTreeWO->mOffsetSize, &PacketTreeWO->mOffsetObject)))
		{
			GS_GOTO_CLEAN();
		}

		if (!!(r = gs_packet_with_offset_get_veclen(PacketTreeWO.get(), &BufferTreeLen)))
			GS_GOTO_CLEAN();

		// FIXME: proper handling for this condition / malformed request or response
		//   presumably server did not send all the requested trees
		GS_ASSERT(BufferTreeLen == Ctx->mClntState->mMissingTreelist->size());

		if (!!(r = clnt_missing_blobs_bare(
			*Ctx->mClntState->mRepositoryT,
			PacketTreeWO->mPacket->data, LengthLimit, PacketTreeWO->mOffsetSize,
			PacketTreeWO->mPacket->data, LengthLimit, PacketTreeWO->mOffsetObject,
			BufferTreeLen,
			MissingBloblist.get())))
		{
			GS_GOTO_CLEAN();
		}

		Ctx->mClntState->mMissingBloblist = MissingBloblist;
		Ctx->mClntState->mTreePacketWithOffset = PacketTreeWO;

		if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_WRITTEN_BLOB_AND_TREE)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_strided_for_oid_vec_cpp(Ctx->mClntState->mMissingBloblist.get(), &MissingBloblistStrided)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_request_blobs3(MissingBloblistStrided, gs_bysize_cb_String, &BysizeBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_CLNT_STATE_CODE_NEED_WRITTEN_BLOB_AND_TREE:
	{
		std::string Buffer;
		uint32_t Offset = 0;

		GS_BYPART_DATA_VAR(String, BysizeBuffer);
		GS_BYPART_DATA_INIT(String, BysizeBuffer, &Buffer);

		if (!!(r = aux_frame_ensure_frametype(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_TYPE_DECL(RESPONSE_BLOBS3_DONE))))
			GS_GOTO_CLEAN();

		GS_ASSERT(Ctx->mClntState->mMissingBloblist && Ctx->mClntState->mWrittenBlob && Ctx->mClntState->mReceivedOneShotBlob && Ctx->mClntState->mMissingTreelist);

		const auto & MissingBloblist = *Ctx->mClntState->mMissingBloblist;
		const auto & ReceivedOneShotBlob = *Ctx->mClntState->mReceivedOneShotBlob;
		      auto & WrittenBlob = *Ctx->mClntState->mWrittenBlob;
		
		if (!(ReceivedOneShotBlob.size() <= MissingBloblist.size() - WrittenBlob.size()))
			GS_ERR_CLEAN(1);

		for (size_t i = 0; i < ReceivedOneShotBlob.size(); i++)
			if (!! git_oid_cmp(&ReceivedOneShotBlob[i], &MissingBloblist[WrittenBlob.size() + i]))
				GS_ERR_CLEAN(1);

		for (size_t i = 0; i < ReceivedOneShotBlob.size(); i++)
			WrittenBlob.push_back(ReceivedOneShotBlob[i]);

		if (WrittenBlob.size() < MissingBloblist.size()) {
			/* not all blobs transferred yet - staying in this state, requesting more blobs */

			/* server may be sending us only a part of the requested blobs (to limit resource use).
			   requesting all remaining blobs every time would be wasteful (consider server always sending just one per response).
			   we limit ourselves to requesting twice as many as we've been sent since the last time.
			   (blobs received since last time are accumulated inside ReceivedOneShotBlob) */
			uint32_t NumAddedToWrittenThisTime = ReceivedOneShotBlob.size();
			uint32_t NumNotYetInWritten = MissingBloblist.size() - WrittenBlob.size();
			uint32_t NumToRequest = GS_MIN(NumAddedToWrittenThisTime * 2, NumNotYetInWritten);

			std::vector<git_oid> BlobsToRequest;
			struct GsStrided BlobsToRequestStrided = {};

			for (size_t i = 0; i < NumToRequest; i++)
				BlobsToRequest.push_back(MissingBloblist[WrittenBlob.size() + i]);

			if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_WRITTEN_BLOB_AND_TREE)))
				GS_GOTO_CLEAN();

			if (!!(r = gs_strided_for_oid_vec_cpp(&BlobsToRequest, &BlobsToRequestStrided)))
				GS_GOTO_CLEAN();

			if (!!(r = aux_frame_full_write_request_blobs3(BlobsToRequestStrided, gs_bysize_cb_String, &BysizeBuffer)))
				GS_GOTO_CLEAN();

			if (!!(r = gs_ev_evbuffer_write_frame(bufferevent_get_output(Bev), Buffer.data(), Buffer.size())))
				GS_GOTO_CLEAN();
		}
		else {
			/* all blobs transferred - moving state forward */

			uint32_t BufferTreeLen = 0;

			if (!!(r = gs_packet_with_offset_get_veclen(Ctx->mClntState->mTreePacketWithOffset.get(), &BufferTreeLen)))
				GS_GOTO_CLEAN();

			GS_ASSERT(BufferTreeLen == Ctx->mClntState->mMissingTreelist->size());

			// FIXME: using full size (PacketTree->dataLength) instead of LengthLimit of PacketTree (NOT of PacketBlob!)
			// FIXME: use last argument of clnt_deserialize_trees to sanity check actual oids of written trees?
			if (!!(r = clnt_deserialize_trees(
				*Ctx->mClntState->mRepositoryT,
				Ctx->mClntState->mTreePacketWithOffset->mPacket->data, Ctx->mClntState->mTreePacketWithOffset->mPacket->dataLength, Ctx->mClntState->mTreePacketWithOffset->mOffsetSize,
				Ctx->mClntState->mTreePacketWithOffset->mPacket->data, Ctx->mClntState->mTreePacketWithOffset->mPacket->dataLength, Ctx->mClntState->mTreePacketWithOffset->mOffsetObject,
				BufferTreeLen, NULL)))
			{
				GS_GOTO_CLEAN();
			}

			/* unlike the others, this state uses reverse convention (initialized as default, cleared for transition) */
			Ctx->mClntState->mWrittenBlob.reset();
			Ctx->mClntState->mWrittenTree.reset();

			if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_UPDATED_REF)))
				GS_GOTO_CLEAN();

			/* no frame needs to be sent */
			goto process_another_state_label;
		}
	}
	break;

	case GS_CLNT_STATE_CODE_NEED_UPDATED_REF:
	{
		sp<git_oid> UpdatedRefOid(new git_oid());

		git_oid CommitOid = {};

		if (!!(r = gs_buf_ensure_haszero(Ctx->mCommonVars.RefNameMainBuf, Ctx->mCommonVars.LenRefNameMain + 1)))
			GS_GOTO_CLEAN();

		if (!!(r = clnt_commit_ensure_dummy(*Ctx->mClntState->mRepositoryT, Ctx->mClntState->mTreeHeadOid.get(), &CommitOid)))
			GS_GOTO_CLEAN();

		if (!!(r = clnt_commit_setref(*Ctx->mClntState->mRepositoryT, Ctx->mCommonVars.RefNameMainBuf, &CommitOid)))
			GS_GOTO_CLEAN();

		Ctx->mClntState->mUpdatedRefOid = UpdatedRefOid;

		if (!!(r = clnt_state_code_ensure(Ctx->mClntState, GS_CLNT_STATE_CODE_NEED_NOTHING)))
			GS_GOTO_CLEAN();

		goto process_another_state_label;
	}
	break;

	case GS_CLNT_STATE_CODE_NEED_NOTHING:
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

int clnt_state_make_default(ClntState *oState) {
	ClntState State;

	State.mWrittenBlob = sp<std::vector<git_oid> >(new std::vector<git_oid>());
	State.mWrittenTree = sp<std::vector<git_oid> >(new std::vector<git_oid>());

	State.mReceivedOneShotBlob = sp<std::vector<git_oid> >(new std::vector<git_oid>());

	if (oState)
		*oState = State;
	return 0;
}

int clnt_state_cpy(ClntState *dst, const ClntState *src) {
	*dst = *src;
	return 0;
}

int clnt_state_code(ClntState *State, uint32_t *oCode) {
	int r = 0;
	
	int Code = 0;

	if (! State->mRepositoryT)
		{ Code = GS_CLNT_STATE_CODE_NEED_REPOSITORY; goto need_repository; }
	if (! State->mTreeHeadOid)
		{ Code = GS_CLNT_STATE_CODE_NEED_TREE_HEAD; goto need_tree_head; }
	if (! State->mTreelist || ! State->mMissingTreelist)
		{ Code = GS_CLNT_STATE_CODE_NEED_TREELIST; goto need_treelist; }
	if (! State->mMissingBloblist || ! State->mTreePacketWithOffset)
		{ Code = GS_CLNT_STATE_CODE_NEED_BLOBLIST; goto need_bloblist; }
	/* unlike the others, this state uses reverse convention (initialized as default, cleared for transition) */
	if (State->mWrittenBlob || State->mWrittenTree)
		{ Code = GS_CLNT_STATE_CODE_NEED_WRITTEN_BLOB_AND_TREE; goto need_written_blob_and_tree; }
	if (! State->mUpdatedRefOid)
		{ Code = GS_CLNT_STATE_CODE_NEED_UPDATED_REF; goto need_updated_ref; }
	if (true)
		{ Code = GS_CLNT_STATE_CODE_NEED_NOTHING; goto need_nothing; }

need_repository:
	if (State->mTreeHeadOid)
		GS_ERR_CLEAN(1);
need_tree_head:
	if (State->mTreelist || State->mMissingTreelist)
		GS_ERR_CLEAN(1);
need_treelist:
	if (State->mMissingBloblist || State->mTreePacketWithOffset)
		GS_ERR_CLEAN(1);
need_bloblist:
	if (! State->mWrittenBlob || ! State->mWrittenTree)
		GS_ERR_CLEAN(1);
need_written_blob_and_tree:
	if (State->mUpdatedRefOid)
		GS_ERR_CLEAN(1);
need_updated_ref:
need_nothing:

	if (oCode)
		*oCode = Code;

clean:

	return r;
}

int clnt_state_code_ensure(ClntState *State, uint32_t WantedCode) {
	int r = 0;

	uint32_t FoundCode = 0;

	if (!!(r = clnt_state_code(State, &FoundCode)))
		GS_GOTO_CLEAN();

	if (WantedCode != FoundCode)
		GS_ERR_CLEAN(1);

clean:

	return r;
}

int gs_ev_ctx_clnt_destroy(struct GsEvCtxClnt *w)
{
	if (w)
		delete w;
	return 0;
}

int gs_ev2_test_clntmain(
	struct GsAuxConfigCommonVars CommonVars,
	struct GsEvCtxClnt **oCtx)
{
	int r = 0;

	struct GsEvCtxClnt *Ctx = new GsEvCtxClnt();

	Ctx->base.mMagic = GS_EV_CTX_CLNT_MAGIC;
	Ctx->base.mIsError = 0;
	Ctx->base.CbConnect = gs_ev_clnt_state_crank3_connected;
	Ctx->base.CbDisconnect = gs_ev_clnt_state_crank3_disconnected;
	Ctx->base.CbCrank = gs_ev_clnt_state_crank3;
	Ctx->base.CbWriteOnlyActive = NULL;
	Ctx->base.CbWriteOnly = NULL;
	Ctx->mCommonVars = CommonVars;
	Ctx->mClntState = new ClntState();

	if (!!(r = clnt_state_make_default(Ctx->mClntState)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_ev2_connect(&Ctx->base,
		CommonVars.ServHostNameBuf, CommonVars.LenServHostName,
		CommonVars.ServPort)))
	{
		GS_ERR_NO_CLEAN(0);
	}

	GS_LOG(I, S, "exiting");

noclean:

	if (oCtx)
		*oCtx = Ctx;

clean:

	return r;
}

