#include <cstring>

#include <vector>
#include <string>

#include <git2.h>
#include <git2/sys/memes.h>

#include <gittest/misc.h>
#include <gittest/filesys.h>
#include <gittest/gittest.h>
#include <gittest/bypart.h>
#include <gittest/bypart_git.h>
#include <gittest/frame.h>
#include <gittest/net4.h>

#define XS_WRITE_ONLY_TYPE_SERV_BLOBS3 0x0BB63409

struct XsConCtxServWriteOnly
{
	std::string mRepositoryPath;
	size_t      mSegmentSizeLimit;

	std::vector<git_oid> mWriting;
	int                  mWritingHead;
	struct XsWriteOnlyDataSendFile mWritingSendFile;
	struct XsWriteOnlyDataBuffer   mWritingHeader;
};

struct XsConCtxServ
{
	struct XsConCtx base;

	struct GsAuxConfigCommonVars mCommonVars;
	git_repository *mRepository;
	git_repository *mRepositorySelfUpdate;

	struct XsConCtxServWriteOnly mWriteOnlyServ;
};

static int cbctxcreate(struct XsConCtx **oCtxBase);
static int cbctxdestroy(struct XsConCtx *CtxBase);
static int cbcrank1(struct XsConCtx *CtxBase, struct GsPacket *Packet);
static int cbwriteonly1(struct XsConCtx *CtxBase);

int gs_write_only_serv_blobs3_init(
	const char *RepositoryPathBuf, size_t LenRepositoryPath,
	size_t SegmentSizeLimit,
	const std::vector<git_oid> &Writing,
	struct XsConCtxServ *Ctx)
{
	int r = 0;

	GS_ASSERT(Ctx->base.mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE);

	Ctx->base.mWriteOnly.mType = XS_WRITE_ONLY_TYPE_SERV_BLOBS3;
	Ctx->mWriteOnlyServ.mRepositoryPath = std::string(RepositoryPathBuf, LenRepositoryPath);
	Ctx->mWriteOnlyServ.mSegmentSizeLimit = SegmentSizeLimit;
	Ctx->mWriteOnlyServ.mWriting = Writing;
	Ctx->mWriteOnlyServ.mWritingHead = -1;
	Ctx->mWriteOnlyServ.mWritingSendFile.mFd = -1;
	Ctx->mWriteOnlyServ.mWritingSendFile.mLen = 0;
	Ctx->mWriteOnlyServ.mWritingSendFile.mOff = 0;
	Ctx->mWriteOnlyServ.mWritingHeader.mBuf = NULL;
	Ctx->mWriteOnlyServ.mWritingHeader.mLen = 0;
	Ctx->mWriteOnlyServ.mWritingHeader.mOff = 0;

clean:

	return r;
}

int gs_write_only_serv_blobs3_reset(
	struct XsConCtxServ *Ctx)
{
	int r = 0;

	if (Ctx->base.mWriteOnly.mType == XS_WRITE_ONLY_TYPE_NONE)
		GS_ERR_NO_CLEAN(0);

	GS_ASSERT(Ctx->base.mWriteOnly.mType == XS_WRITE_ONLY_TYPE_SERV_BLOBS3);

	if (Ctx->mWriteOnlyServ.mWritingSendFile.mFd != -1)
		gs_posixstyle_close(Ctx->mWriteOnlyServ.mWritingSendFile.mFd);
	free(Ctx->mWriteOnlyServ.mWritingHeader.mBuf);

	Ctx->base.mWriteOnly.mType = XS_WRITE_ONLY_TYPE_NONE;
	Ctx->mWriteOnlyServ.mRepositoryPath.clear();
	Ctx->mWriteOnlyServ.mSegmentSizeLimit = 0;
	Ctx->mWriteOnlyServ.mWriting.clear();
	Ctx->mWriteOnlyServ.mWritingHead = -1;
	Ctx->mWriteOnlyServ.mWritingSendFile.mFd = -1;
	Ctx->mWriteOnlyServ.mWritingSendFile.mLen = 0;
	Ctx->mWriteOnlyServ.mWritingSendFile.mOff = 0;
	Ctx->mWriteOnlyServ.mWritingHeader.mBuf = NULL;
	Ctx->mWriteOnlyServ.mWritingHeader.mLen = 0;
	Ctx->mWriteOnlyServ.mWritingHeader.mOff = 0;

noclean:

clean:

	return r;
}

int gs_write_only_serv_blobs3_advance(
	struct XsConCtxServ *Ctx,
	int *oHeadIsNew)
{
	/* caller inspects WritingHead >= Writing.size() for completion testing and */
	int r = 0;

	int HeadIsNew = 0;

	/* initial condition (WritingHead -1) or having finished a segment (Off == Len) */
	if (Ctx->mWriteOnlyServ.mWritingHead == -1 ||
		(Ctx->mWriteOnlyServ.mWritingSendFile.mOff == Ctx->mWriteOnlyServ.mWritingSendFile.mLen))
	{
		/* advance writing head */
		if (++Ctx->mWriteOnlyServ.mWritingHead >= Ctx->mWriteOnlyServ.mWriting.size())
			GS_ERR_NO_CLEAN(0);

		HeadIsNew = 1;

		if (1) {
			/* clear old sendfile info */
			if (Ctx->mWriteOnlyServ.mWritingSendFile.mFd != -1)
				gs_posixstyle_close(Ctx->mWriteOnlyServ.mWritingSendFile.mFd);
			Ctx->mWriteOnlyServ.mWritingSendFile.mFd = -1;
			Ctx->mWriteOnlyServ.mWritingSendFile.mLen = 0;
			Ctx->mWriteOnlyServ.mWritingSendFile.mOff = 0;
		}
		if (1) {
			/* set new sendfile info */
			char PathBuf[512] = {};
			size_t LenPath = 0;
			struct gs_stat Stat = {};

			if (!!(r = git_memes_objpath(
				Ctx->mWriteOnlyServ.mRepositoryPath.data(), Ctx->mWriteOnlyServ.mRepositoryPath.size(),
				& Ctx->mWriteOnlyServ.mWriting[Ctx->mWriteOnlyServ.mWritingHead],
				PathBuf, sizeof PathBuf, &LenPath)))
			{
				GS_GOTO_CLEAN();
			}

			if (-1 == (Ctx->mWriteOnlyServ.mWritingSendFile.mFd = gs_posixstyle_open_read(PathBuf)))
				GS_ERR_CLEAN(1);

			if (-1 == gs_posixstyle_fstat(Ctx->mWriteOnlyServ.mWritingSendFile.mFd, &Stat))
				GS_ERR_CLEAN(1);

			if (! Stat.mStMode_IfReg)
				GS_ERR_CLEAN(1);

			Ctx->mWriteOnlyServ.mWritingSendFile.mLen = Stat.mStSize;
			Ctx->mWriteOnlyServ.mWritingSendFile.mOff = 0;
		}
	}

noclean:
	if (oHeadIsNew)
		*oHeadIsNew = HeadIsNew;

clean:

	return r;
}

int gs_net4_serv_state_service_request_blobs3(
	struct XsConCtxServ *Ctx,
	struct GsPacket *Packet,
	uint32_t OffsetSize,
	git_repository *Repository,
	const struct GsFrameType FrameTypeResponse)
{
	int r = 0;

	const char *RepositoryPathBuf = NULL;
	size_t LenRepositoryPath = 0;

	std::string ResponseBuffer;
	uint32_t Offset = OffsetSize;
	uint32_t LengthLimit = 0;
	std::vector<git_oid> BloblistRequested;
	std::string SizeBufferBlob;
	std::string ObjectBufferBlob;

	size_t ServBlobSoftSizeLimit = Ctx->mCommonVars.ServBlobSoftSizeLimit;
	size_t NumUntilSizeLimit = 0;

	RepositoryPathBuf = git_repository_path(Repository);
	LenRepositoryPath = strlen(RepositoryPathBuf);

	GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
	GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

	GS_BYPART_DATA_VAR(OidVector, BypartBloblistRequested);
	GS_BYPART_DATA_INIT(OidVector, BypartBloblistRequested, &BloblistRequested);

	if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_read_oid_vec(Packet->data, LengthLimit, Offset, &Offset, &BypartBloblistRequested, gs_bypart_cb_OidVector)))
		GS_GOTO_CLEAN();

	/* the client may be requesting many (and large) blobs
	   so we have a size limit (for transmission in one response) */

	if (!!(r = aux_objects_until_sizelimit(Repository, BloblistRequested.data(), BloblistRequested.size(), ServBlobSoftSizeLimit, &NumUntilSizeLimit)))
		GS_GOTO_CLEAN();

	/* if none made it under the size limit, make sure we send at least one, so that progress is made */

	if (NumUntilSizeLimit == 0)
		NumUntilSizeLimit = GS_CLAMP(BloblistRequested.size(), 0, 1);

	BloblistRequested.resize(NumUntilSizeLimit);

	// FIXME: reusing size limit with different semantics - have a separate one
	if (!!(r = gs_write_only_serv_blobs3_init(RepositoryPathBuf, LenRepositoryPath, ServBlobSoftSizeLimit, BloblistRequested, Ctx)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_net4_serv_state_crank3(
	struct XsConCtx *CtxBase,
	struct GsPacket *Packet)
{
	int r = 0;

	struct XsConCtxServ *Ctx = (struct XsConCtxServ *) CtxBase;

	uint32_t OffsetStart = 0;
	uint32_t OffsetSize = 0;

	struct GsFrameType FoundFrameType = {};

	if (!!(r = aux_frame_read_frametype(Packet->data, Packet->dataLength, OffsetStart, &OffsetSize, &FoundFrameType)))
		GS_GOTO_CLEAN();

	switch (FoundFrameType.mTypeNum)
	{

	case GS_FRAME_TYPE_REQUEST_LATEST_COMMIT_TREE:
	{
		std::string ResponseBuffer;
		uint32_t Offset = OffsetSize;
		git_oid CommitHeadOid = {};
		git_oid TreeHeadOid = {};

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, 0)))
			GS_GOTO_CLEAN();

		if (!!(r = serv_latest_commit_tree_oid(Ctx->mRepository, Ctx->mCommonVars.RefNameMainBuf, &CommitHeadOid, &TreeHeadOid)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_response_latest_commit_tree(TreeHeadOid.id, GIT_OID_RAWSZ, gs_bysize_cb_String, &BysizeResponseBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_FRAME_TYPE_REQUEST_TREELIST:
	{
		std::string ResponseBuffer;
		uint32_t Offset = OffsetSize;
		git_oid TreeOid = {};
		std::vector<git_oid> Treelist;
		GsStrided TreelistStrided = {};

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, GS_PAYLOAD_OID_LEN)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_oid(Packet->data, Packet->dataLength, Offset, &Offset, TreeOid.id, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();

		if (!!(r = serv_oid_treelist(Ctx->mRepository, &TreeOid, &Treelist)))
			GS_GOTO_CLEAN();

		GS_LOG(I, PF, "listing trees [num=%d]", (int)Treelist.size());

		if (!!(r = gs_strided_for_oid_vec_cpp(&Treelist, &TreelistStrided)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_response_treelist(TreelistStrided, gs_bysize_cb_String, &BysizeResponseBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_FRAME_TYPE_REQUEST_TREES:
	{
		std::string ResponseBuffer;
		uint32_t Offset = OffsetSize;
		uint32_t LengthLimit = 0;
		std::vector<git_oid> TreelistRequested;
		std::string SizeBufferTree;
		std::string ObjectBufferTree;

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		GS_BYPART_DATA_VAR(OidVector, BypartTreelistRequested);
		GS_BYPART_DATA_INIT(OidVector, BypartTreelistRequested, &TreelistRequested);

		if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_read_oid_vec(Packet->data, LengthLimit, Offset, &Offset, &BypartTreelistRequested, gs_bypart_cb_OidVector)))
			GS_GOTO_CLEAN();

		if (!!(r = serv_serialize_trees(Ctx->mRepository, &TreelistRequested, &SizeBufferTree, &ObjectBufferTree)))
			GS_GOTO_CLEAN();

		GS_LOG(I, PF, "serializing trees [num=%d]", (int)TreelistRequested.size());

		if (!!(r = aux_frame_full_write_response_trees(
			TreelistRequested.size(),
			(uint8_t *)SizeBufferTree.data(), SizeBufferTree.size(),
			(uint8_t *)ObjectBufferTree.data(), ObjectBufferTree.size(),
			gs_bysize_cb_String, &BysizeResponseBuffer)))
		{
			GS_GOTO_CLEAN();
		}

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_FRAME_TYPE_REQUEST_BLOBS3:
	{
		if (!!(r = gs_net4_serv_state_service_request_blobs3(Ctx, Packet, OffsetSize, Ctx->mRepository, GS_FRAME_TYPE_DECL(RESPONSE_BLOBS3))))
			GS_GOTO_CLEAN();
	}
	break;

	case GS_FRAME_TYPE_REQUEST_LATEST_SELFUPDATE_BLOB:
	{
		std::string ResponseBuffer;
		uint32_t Offset = OffsetSize;
		git_oid CommitHeadOid = {};
		git_oid TreeHeadOid = {};
		git_oid BlobSelfUpdateOid = {};

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, 0)))
			GS_GOTO_CLEAN();

		if (!!(r = serv_latest_commit_tree_oid(Ctx->mRepositorySelfUpdate, Ctx->mCommonVars.RefNameSelfUpdateBuf, &CommitHeadOid, &TreeHeadOid)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_oid_tree_blob_byname(Ctx->mRepositorySelfUpdate, &TreeHeadOid, Ctx->mCommonVars.SelfUpdateBlobNameBuf, &BlobSelfUpdateOid)))
			GS_GOTO_CLEAN();

		if (!!(r = aux_frame_full_write_response_latest_selfupdate_blob(BlobSelfUpdateOid.id, GIT_OID_RAWSZ, gs_bysize_cb_String, &BysizeResponseBuffer)))
			GS_GOTO_CLEAN();

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN();
	}
	break;

	default:
		GS_ASSERT(0);
	}

clean:

	return r;
}

int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type)
{
	struct XsConCtxServ *Ctx = new XsConCtxServ();

	Ctx->base.CbCtxCreate = cbctxcreate;
	Ctx->base.CbCtxDestroy = cbctxdestroy;
	Ctx->base.CbCrank = cbcrank1;
	Ctx->base.CbWriteOnly = cbwriteonly1;

	*oCtxBase = &Ctx->base;

	return 0;
}

int cbctxdestroy(struct XsConCtx *CtxBase)
{
	struct XsConCtxServ *Ctx = (struct XsConCtxServ *) CtxBase;

	GS_DELETE(&Ctx, struct XsConCtxServ);

	return 0;
}

int cbcrank1(struct XsConCtx *CtxBase, struct GsPacket *Packet)
{
	int r = 0;

	struct XsConCtxServ *Ctx = (struct XsConCtxServ *) CtxBase;

	if (!!(r = gs_net4_serv_state_crank3(CtxBase, Packet)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int cbwriteonly1(struct XsConCtx *CtxBase)
{
	int r = 0;

	struct XsConCtxServ *Ctx = (struct XsConCtxServ *) CtxBase;

	struct XsWriteOnly *WriteOnly = &Ctx->base.mWriteOnly;
	struct XsConCtxServWriteOnly *WriteOnlyServ = &Ctx->mWriteOnlyServ;

	switch (WriteOnly->mType)
	{

	case XS_WRITE_ONLY_TYPE_BUFFER:
	{
		struct XsWriteOnlyDataBuffer *Buffer = &WriteOnly->mData.mBuffer;

		if (!! xs_write_only_data_buffer_advance(Ctx->base.mFd, Buffer))
			assert(0);

		if (Buffer->mOff == Buffer->mLen) {
			/* finished */
			if (!!(r = xs_write_only_data_buffer_reset(Buffer)))
				GS_GOTO_CLEAN();
			WriteOnly->mType = XS_WRITE_ONLY_TYPE_NONE;
		}
	}
	break;

	case XS_WRITE_ONLY_TYPE_SERV_BLOBS3:
	{
		int HeadIsNew = 0;

		if (WriteOnlyServ->mWritingHeader.mBuf) {
			/* header sending behavior */
			if (!!(r = xs_write_only_data_buffer_advance(Ctx->base.mFd, &WriteOnlyServ->mWritingHeader)))
				GS_GOTO_CLEAN();
			if (WriteOnlyServ->mWritingHeader.mOff == WriteOnlyServ->mWritingHeader.mLen)
				if (!!(r = xs_write_only_data_buffer_reset(&WriteOnlyServ->mWritingHeader)))
					GS_GOTO_CLEAN();
		}

		// FIXME: error handling
		if (!!(r = gs_write_only_serv_blobs3_advance(Ctx, &HeadIsNew)))
			GS_GOTO_CLEAN();

		if (HeadIsNew) {
			/* activate header sending behavior */
			const size_t PayloadLen = GIT_OID_RAWSZ + Ctx->mWriteOnlyServ.mWritingSendFile.mLen;

			std::string HdrBuffer;
			GS_BYPART_DATA_VAR(String, BpBuffer);
			GS_BYPART_DATA_INIT(String, BpBuffer, &HdrBuffer);

			char HdrOuterBuf[9] = {};
			size_t LenHdrOuter = 0;

			if (!!(r = aux_frame_part_write_for_payload(GS_FRAME_TYPE_DECL(RESPONSE_BLOBS3), PayloadLen, gs_bysize_cb_String, &BpBuffer)))
				GS_GOTO_CLEAN();

			if (!!(r = xs_net4_write_frame_outer_header(HdrBuffer.size() + PayloadLen, HdrOuterBuf, sizeof HdrOuterBuf, &LenHdrOuter)))
				GS_GOTO_CLEAN();

			if (!!(r = xs_write_only_data_buffer_init_copying2(&WriteOnlyServ->mWritingHeader, HdrOuterBuf, LenHdrOuter, HdrBuffer.data(), HdrBuffer.size())))
				GS_GOTO_CLEAN();
		}
		else if (WriteOnlyServ->mWritingHead < WriteOnlyServ->mWriting.size()) {
			/* payload sending behavior */
			if (!!(r = xs_write_only_data_send_file_advance(Ctx->base.mFd, &WriteOnlyServ->mWritingSendFile)))
				GS_GOTO_CLEAN();
		}
		else {
			/* finished */
			GS_ASSERT(Ctx->mWriteOnlyServ.mWritingHead >= Ctx->mWriteOnlyServ.mWriting.size());
			if (!!(r = gs_write_only_serv_blobs3_reset(Ctx)))
				GS_GOTO_CLEAN();
		}
	}
	break;

	default:
		GS_ASSERT(0);
	}

clean:

	return r;
}

int gs_net4_serv_start()
{
	int r = 0;

	int ListenFd = -1;
	struct XsServCtl *ServCtl = NULL;

	if (!!(r = xs_net4_socket_listen_create("3384", &ListenFd)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_net4_listenme(ListenFd, cbctxcreate, &ServCtl)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_serv_ctl_quit_wait(ServCtl)))
		GS_GOTO_CLEAN();

clean:
	GS_DELETE_F(&ServCtl, xs_serv_ctl_destroy);

	return r;
}
