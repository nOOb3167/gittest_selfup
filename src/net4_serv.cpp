#include <cstring>

#include <mutex>
#include <vector>
#include <string>
#include <map>

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

struct XsCacheHead
{
	git_oid mOid;
	struct GsTreeInflatedNode * mNodeList;
	std::map<git_oid *, GsTreeInflated *, oid_comparator_t> mOidMap;
};

struct XsCacheHeadDeleter
{
	void operator()(struct XsCacheHead *CacheHead) {
		GS_DELETE_F(&CacheHead->mNodeList, gs_tree_inflated_node_list_destroy);
		GS_DELETE(&CacheHead, struct XsCacheHead);
	}
};

struct XsCacheHeadAux
{
	sp<XsCacheHead> mHead;
	std::mutex mMutex;
};

struct XsConExtServ
{
	struct XsConExt base;

	struct XsCacheHeadAux mCacheHead;
};

struct XsConCtxServ
{
	struct XsConCtx base;
	struct XsConExtServ *mExt;

	struct GsAuxConfigCommonVars mCommonVars;
	git_repository *mRepository;
	git_repository *mRepositorySelfUpdate;

	struct XsConCtxServWriteOnly mWriteOnlyServ;
};

static int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type);
static int cbctxdestroy(struct XsConCtx *CtxBase);
static int cbcrank1(struct XsConCtx *CtxBase, struct GsPacket *Packet);
static int cbwriteonly1(struct XsConCtx *CtxBase);

int gs_cache_head_aux_refresh(
	struct XsCacheHeadAux *CacheHeadAux,
	const char *RepositoryPath, size_t LenRepository,
	git_oid *WantedTreeHeadOid)
{
	int r = 0;

	sp<XsCacheHead> Head;
	struct GsTreeInflatedNode *NodeList = NULL;
	sp<XsCacheHead> NewCacheHead;

	/* double-checked locking */

	Head = CacheHeadAux->mHead;
	if (!Head || git_oid_cmp(&Head->mOid, WantedTreeHeadOid) == 0) {
		GS_ERR_NO_CLEAN(0);
	}
	else {
		std::unique_lock<std::mutex> Lock(CacheHeadAux->mMutex);
		Head = CacheHeadAux->mHead;
		if (!Head || git_oid_cmp(&Head->mOid, WantedTreeHeadOid) == 0)
			GS_ERR_NO_CLEAN(0);

		if (!!(r = gs_treelist(RepositoryPath, LenRepository, WantedTreeHeadOid, &NodeList)))
			GS_GOTO_CLEAN();

		NewCacheHead = sp<XsCacheHead>(new XsCacheHead(), struct XsCacheHeadDeleter());
		NewCacheHead->mNodeList = GS_ARGOWN(&NodeList);
		git_oid_cpy(&NewCacheHead->mOid, WantedTreeHeadOid);
		NewCacheHead->mOidMap.clear();
		for (struct GsTreeInflatedNode *it = NewCacheHead->mNodeList; it; it = it->mNext)
			NewCacheHead->mOidMap.insert(std::make_pair(&it->mData->mOid, it->mData));


		CacheHeadAux->mHead = NewCacheHead;
	}

noclean:

clean:
	GS_DELETE_F(&NodeList, gs_tree_inflated_node_list_destroy);

	return r;
}

int gs_trees_from_existing_or_fresh_aux_allzero(struct GsTreeInflated **FreshPairedVec, size_t NumFreshPaired)
{
	for (size_t i = 0; i < NumFreshPaired; i++)
		if (FreshPairedVec[i] != NULL)
			return 1;
	return 0;
}

int gs_cache_head_trees_cached_or_fresh(
	struct XsCacheHead *Head,
	const char *RepositoryPathBuf, size_t LenRepositoryPath,
	git_oid *RequestedVec, size_t NumRequested,
	struct GsTreeInflated **ioFreshPairedVec, size_t NumFreshPaired,
	struct GsTreeInflated **ioOutputPairedVecNotOwned, size_t NumOutputPaired)
{
	int r = 0;

	GS_ASSERT(NumRequested == NumFreshPaired && NumRequested == NumOutputPaired);
	GS_ASSERT(! gs_trees_from_existing_or_fresh_aux_allzero(ioFreshPairedVec, NumFreshPaired));

	struct GsTreeInflated *FreshTree = NULL;
	size_t FreshPairedIdx = 0;

	for (size_t i = 0; i < NumRequested; i++) {
		auto it = Head->mOidMap.find(RequestedVec + i);
		if (it != Head->mOidMap.end()) {
			ioOutputPairedVecNotOwned[i] = it->second;
		}
		else {
			if (!!(r = gs_git_read_tree(
				RepositoryPathBuf, LenRepositoryPath,
				RequestedVec + i,
				GS_FIXME_ARBITRARY_TREE_MAX_SIZE_LIMIT,
				true,
				&FreshTree)))
			{
				GS_GOTO_CLEAN();
			}
			ioOutputPairedVecNotOwned[i] = GS_ARGOWN(&FreshTree);
			ioFreshPairedVec[FreshPairedIdx++] = ioOutputPairedVecNotOwned[i];
		}
	}

clean:
	if (!!r) {
		for (size_t i = 0; i < NumFreshPaired; i++)
			GS_DELETE_F(&ioFreshPairedVec[i], gs_tree_inflated_destroy);
	}

	GS_DELETE_F(&FreshTree, gs_tree_inflated_destroy);

	return r;
}

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

		if (!!(r = gs_latest_commit_tree_oid(
			git_repository_path(Ctx->mRepository), strlen(git_repository_path(Ctx->mRepository)),
			Ctx->mCommonVars.RefNameMainBuf, Ctx->mCommonVars.LenRefNameMain,
			&CommitHeadOid, &TreeHeadOid)))
		{
			GS_GOTO_CLEAN();
		}

		if (!!(r = gs_cache_head_aux_refresh(
			&Ctx->mExt->mCacheHead,
			git_repository_path(Ctx->mRepository), strlen(git_repository_path(Ctx->mRepository)),
			&TreeHeadOid)))
		{
			GS_GOTO_CLEAN();
		}

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
		struct GsTreeInflatedNode *TreeList = NULL;
		struct GsTreeInflatedNode *TreeListChosenNotOwned = NULL; /*notowned*/
		std::vector<git_oid> TreeVec;
		GsStrided TreeVecStrided = {};
		const sp<XsCacheHead> Head = Ctx->mExt->mCacheHead.mHead; /*MTsafety*/

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		if (!!(r = aux_frame_read_size_ensure(Packet->data, Packet->dataLength, Offset, &Offset, GS_PAYLOAD_OID_LEN)))
			GS_GOTO_CLEAN_J(treelist);

		if (!!(r = aux_frame_read_oid(Packet->data, Packet->dataLength, Offset, &Offset, TreeOid.id, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN_J(treelist);

		/* serve from cache if applicable */

		if (git_oid_cmp(&TreeOid, &Head->mOid) == 0) {
			TreeListChosenNotOwned = Head->mNodeList;
		}
		else {
			if (!!(r = gs_treelist(git_repository_path(Ctx->mRepository), strlen(git_repository_path(Ctx->mRepository)), &TreeOid, &TreeList)))
				GS_GOTO_CLEAN_J(treelist);
			TreeListChosenNotOwned = TreeList;
		}

		for (struct GsTreeInflatedNode *it = TreeListChosenNotOwned; it; it = it->mNext)
			TreeVec.push_back(it->mData->mOid);

		GS_LOG(I, PF, "listing trees [num=%d]", (int)TreeVec.size());

		if (!!(r = gs_strided_for_oid_vec_cpp(&TreeVec, &TreeVecStrided)))
			GS_GOTO_CLEAN_J(treelist);

		if (!!(r = aux_frame_full_write_response_treelist(TreeVecStrided, gs_bysize_cb_String, &BysizeResponseBuffer)))
			GS_GOTO_CLEAN_J(treelist);

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN_J(treelist);

	clean_treelist:
		GS_DELETE_F(&TreeList, gs_tree_inflated_node_list_destroy);
		if (!!r)
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
		const sp<XsCacheHead> Head = Ctx->mExt->mCacheHead.mHead; /*MTsafety*/
		std::vector<GsTreeInflated *> TreeVecFresh;
		std::vector<GsTreeInflated *> TreeVecNotOwned;

		GS_BYPART_DATA_VAR(String, BysizeResponseBuffer);
		GS_BYPART_DATA_INIT(String, BysizeResponseBuffer, &ResponseBuffer);

		GS_BYPART_DATA_VAR(OidVector, BypartTreelistRequested);
		GS_BYPART_DATA_INIT(OidVector, BypartTreelistRequested, &TreelistRequested);

		if (!!(r = aux_frame_read_size_limit(Packet->data, Packet->dataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &LengthLimit)))
			GS_GOTO_CLEAN_J(treelist);

		if (!!(r = aux_frame_read_oid_vec(Packet->data, LengthLimit, Offset, &Offset, &BypartTreelistRequested, gs_bypart_cb_OidVector)))
			GS_GOTO_CLEAN_J(treelist);

		TreeVecFresh.resize(TreelistRequested.size());
		TreeVecNotOwned.resize(TreelistRequested.size());

		if (!!(r = gs_cache_head_trees_cached_or_fresh(
			Head.get(),
			git_repository_path(Ctx->mRepository), strlen(git_repository_path(Ctx->mRepository)),
			TreelistRequested.data(), TreelistRequested.size(),
			TreeVecFresh.data(), TreeVecFresh.size(),
			TreeVecNotOwned.data(), TreeVecNotOwned.size())))
		{
			GS_GOTO_CLEAN_J(treelist);
		}

		if (!!(r = gs_tree_inflated_vec_serialize(TreeVecNotOwned.data(), TreeVecNotOwned.size(), &SizeBufferTree, &ObjectBufferTree)))
			GS_GOTO_CLEAN_J(treelist);

		GS_LOG(I, PF, "serializing trees [requested=%d, serialized=%d]", (int)TreelistRequested.size(), (int)TreeVecNotOwned.size());

		if (!!(r = aux_frame_full_write_response_trees(
			TreelistRequested.size(),
			(uint8_t *)SizeBufferTree.data(), SizeBufferTree.size(),
			(uint8_t *)ObjectBufferTree.data(), ObjectBufferTree.size(),
			gs_bysize_cb_String, &BysizeResponseBuffer)))
		{
			GS_GOTO_CLEAN_J(treelist);
		}

		if (!!(r = xs_write_only_data_buffer_init_copying(& Ctx->base.mWriteOnly, ResponseBuffer.data(), ResponseBuffer.size())))
			GS_GOTO_CLEAN_J(treelist);

	clean_treelist:
		for (size_t i = 0; i < TreeVecFresh.size(); i++)
			GS_DELETE_F(&TreeVecFresh[i], gs_tree_inflated_destroy);
		if (!!r)
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

int cbctxcreate(struct XsConCtx **oCtxBase, enum XsSockType Type, struct XsConExt *ExtBase)
{
	struct XsConCtxServ *Ctx = new XsConCtxServ();

	Ctx->base.CbCtxCreate = cbctxcreate;
	Ctx->base.CbCtxDestroy = cbctxdestroy;
	Ctx->base.CbCrank = cbcrank1;
	Ctx->base.CbWriteOnly = cbwriteonly1;

	Ctx->mExt = (struct XsConExtServ *) ExtBase;

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
	struct XsConExtServ *Ext = NULL;
	struct XsServCtl *ServCtl = NULL;

	if (!!(r = xs_net4_socket_listen_create("3384", &ListenFd)))
		GS_GOTO_CLEAN();

	Ext = new XsConExtServ();
	if (!!(r = xs_con_ext_base_init(&Ext->base)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_net4_listenme(ListenFd, cbctxcreate, &Ext->base, &ServCtl)))
		GS_GOTO_CLEAN();

	if (!!(r = xs_serv_ctl_quit_wait(ServCtl)))
		GS_GOTO_CLEAN();

clean:
	GS_DELETE_F(&ServCtl, xs_serv_ctl_destroy);

	return r;
}
