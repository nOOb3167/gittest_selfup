#ifdef _MSC_VER
#pragma warning(disable : 4267 4102)  // conversion from size_t, unreferenced label
#endif /* _MSC_VER */

#include <cstdlib>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <climits>  // ULLONG_MAX

#include <algorithm>
#include <vector>
#include <map>
#include <set>
#include <list>
#include <string>
#include <utility>

#include <git2.h>
#include <git2/sys/repository.h>  /* git_repository_new (no backends so custom may be added) */
#include <git2/sys/mempack.h>     /* in-memory backend */
#include <git2/sys/memes.h>       /* git_memes_thunderdome */

#include <gittest/misc.h>
#include <gittest/bypart.h>
#include <gittest/bypart_git.h>
#include <gittest/filesys.h>
#include <gittest/gittest.h>

/*
= git init =
fresh repositories have no "refs/heads/master" ref
= resetting the git repo (nuke loose objects but packs remain) =
git gc --prune=all
*/

struct GsReachRefsCbCtx
{
	git_repository       *Repository;
	toposet_t            *MarkSet;
	std::vector<git_oid> *ReachableOid;
};

static int gs_reach_refs_cb(git_reference *Ref, void *ctx);
static int gs_reach_oid(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, const git_oid *Oid);
static int gs_reach_tree_blobs(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, git_tree *Tree);
static int gs_reach_commit_tree(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, git_commit *Commit);
static int gs_reach_refs_cb(git_reference *Ref, void *ctx);

int gs_reach_oid(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, const git_oid *Oid)
{
	if (MarkSet->find(Oid) == MarkSet->end()) {
		MarkSet->insert(Oid);
		ReachableOid->push_back(*Oid);
	}
	return 0;
}

int gs_reach_tree_blobs(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, git_tree *Tree)
{
	int r = 0;

	git_odb *Odb = NULL;

	if (!!(r = git_repository_odb(&Odb, Repository)))
		GS_GOTO_CLEAN();

	for (uint32_t j = 0; j < git_tree_entrycount(Tree); j++) {
		const git_tree_entry *Entry = git_tree_entry_byindex(Tree, j);
		const git_oid *EntryOid = git_tree_entry_id(Entry);
		const git_otype EntryType = git_tree_entry_type(Entry);
		if (EntryType == GIT_OBJ_TREE)
			continue;
		GS_ASSERT(EntryType == GIT_OBJ_BLOB);
		if (!!(r = gs_reach_oid(Repository, MarkSet, ReachableOid, EntryOid)))
			GS_GOTO_CLEAN();
	}

clean:
	git_odb_free(Odb);

	return r;
}

int gs_reach_commit_tree(git_repository *Repository, toposet_t *MarkSet, std::vector<git_oid> *ReachableOid, git_commit *Commit)
{
	int r = 0;

	git_tree *Tree = NULL;

	topolist_t NodeList;

	if (!!(r = git_commit_tree(&Tree, Commit)))
		GS_GOTO_CLEAN();

	if (!!(r = tree_toposort(Repository, Tree, &NodeList)))
		GS_GOTO_CLEAN();

	for (topolist_t::iterator it = NodeList.begin(); it != NodeList.end(); ++it) {
		if (!!(r = gs_reach_oid(Repository, MarkSet, ReachableOid, git_tree_id(*it))))
			GS_GOTO_CLEAN();
		if (!!(r = gs_reach_tree_blobs(Repository, MarkSet, ReachableOid, *it)))
			GS_GOTO_CLEAN();
	}

clean:
	for (topolist_t::iterator it = NodeList.begin(); it != NodeList.end(); ++it)
		git_tree_free(*it);

	return r;
}

int gs_reach_refs_cb(git_reference *Ref, void *ctx)
{
	int r = 0;

	struct GsReachRefsCbCtx *Ctx = (struct GsReachRefsCbCtx *) ctx;

	git_reference *RefResolved = NULL;
	git_commit    *Commit = NULL;

	if (!!(r = git_reference_resolve(&RefResolved, Ref)))
		GS_GOTO_CLEAN();

	GS_ASSERT(git_reference_type(RefResolved) == GIT_REF_OID);

	if (!!(r = git_commit_lookup(&Commit, Ctx->Repository, git_reference_target(RefResolved))))
		GS_GOTO_CLEAN();

	if (!!(r = gs_reach_oid(Ctx->Repository, Ctx->MarkSet, Ctx->ReachableOid, git_commit_id(Commit))))
		GS_GOTO_CLEAN();

	if (!!(r = gs_reach_commit_tree(Ctx->Repository, Ctx->MarkSet, Ctx->ReachableOid, Commit)))
		GS_GOTO_CLEAN();

clean:
	git_commit_free(Commit);
	git_reference_free(RefResolved);

	return r;
}

int gs_reach_refs(git_repository *Repository, gs_bypart_cb_t cb, void *ctx)
{
	int r = 0;

	std::vector<git_oid> ReachableOid;

	toposet_t MarkSet;

	struct GsReachRefsCbCtx Ctx = {};
	Ctx.Repository = Repository;
	Ctx.MarkSet = &MarkSet;
	Ctx.ReachableOid = &ReachableOid;

	if (!!(r = git_reference_foreach(Repository, gs_reach_refs_cb, &Ctx)))
		GS_GOTO_CLEAN();

	for (size_t i = 0; i < ReachableOid.size(); ++i)
		if (!!(r = cb(ctx, (const char *) &ReachableOid[i], sizeof ReachableOid[0])))
			GS_GOTO_CLEAN();

clean:

	return r;
}

/* takes ownership of 'Tree' on success (list responsible for disposal) */
int tree_toposort_visit(git_repository *Repository, toposet_t *MarkSet, topolist_t *NodeList, git_tree *Tree) {
	int r = 0;
	/* = if n is not marked (i.e. has not been visited yet) then = */
	if (MarkSet->find(git_tree_id(Tree)) == MarkSet->end()) {
		/* = mark n = */
		MarkSet->insert(git_tree_id(Tree));
		/* = for each node m with an edge from n to m do = */
		size_t entrycount = git_tree_entrycount(Tree);
		for (int i = 0; i < entrycount; i++) {
			git_tree *TreeSubtree = NULL;
			if (git_tree_entry_type(git_tree_entry_byindex(Tree, i)) != GIT_OBJ_TREE)
				continue;
			if (!!(r = git_tree_lookup(&TreeSubtree, Repository, git_tree_entry_id(git_tree_entry_byindex(Tree, i)))))
				goto cleansub;
			/* = visit(m) = */
			/* ownership of 'Tree' by recursive call below taken only on success. therefore on failure we can free. */
			if (!!(r = tree_toposort_visit(Repository, MarkSet, NodeList, TreeSubtree)))
				goto cleansub;
		cleansub:
			if (!!r) {
				if (TreeSubtree)
					git_tree_free(TreeSubtree);
			}
			if (!!r)
				goto clean;
		}
		/* = add n to head of L = */
		NodeList->push_front(Tree);
	}

clean:

	return r;
}

int tree_toposort(git_repository *Repository, git_tree *Tree, topolist_t *oNodeList) {
	/* https://en.wikipedia.org/wiki/Topological_sorting#Depth-first_search */
	int r = 0;
	toposet_t MarkSet; /* filled by tree-scoped git_oid - no need to free */
	topolist_t NodeList; /* filled by owned git_tree - must free */
	if (!!(r = tree_toposort_visit(Repository, &MarkSet, &NodeList, Tree)))
		goto clean;

	if (oNodeList)
		oNodeList->swap(NodeList);

clean:
	if (!!r) {
		for (topolist_t::iterator it = NodeList.begin(); it != NodeList.end(); it++)
			git_tree_free(*it);
	}

	return r;
}

/* takes ownership of 'Tree' */
int tree_toposort_visit2(const char *RepositoryPathBuf, size_t LenRepositoryPath, toposet_t *MarkSet, struct GsTreeInflatedNode **NodeList, struct GsTreeInflated *Tree)
{
	int r = 0;
	struct GsTreeInflatedNode *NewNode = NULL;
	/* = if n is not marked (i.e. has not been visited yet) then = */
	if (MarkSet->find(&Tree->mOid) == MarkSet->end()) {
		/* = mark n = */
		MarkSet->insert(&Tree->mOid);
		/* = for each node m with an edge from n to m do = */
		size_t Offset = 0;
		unsigned long long Mode = 0;
		const char *FileName = NULL;
		size_t FileNameLen = 0;
		git_oid TreeOid = {};
		while (!(r = git_memes_tree(Tree->mDataBuf, Tree->mLenData, &Offset, &Mode, &FileName, &FileNameLen, &TreeOid)) && Offset != -1) {
			struct GsTreeInflated *TreeSubTree = NULL;
			if (Mode != GIT_FILEMODE_TREE)
				continue;
			if (!!(r = gs_git_read_tree(RepositoryPathBuf, LenRepositoryPath, &TreeOid, GS_FIXME_ARBITRARY_TREE_MAX_SIZE_LIMIT, true, &TreeSubTree)))
				goto cleansub;
			/* = visit(m) = */
			if (!!(r = tree_toposort_visit2(RepositoryPathBuf, LenRepositoryPath, MarkSet, NodeList, GS_ARGOWN(&TreeSubTree))))
				goto cleansub;
		cleansub:
			GS_DELETE_F(&TreeSubTree, gs_tree_inflated_destroy);
			if (!!r)
				goto clean;
		}
		/* = add n to head of L = */
		if (!(NewNode = (struct GsTreeInflatedNode *) calloc(sizeof *NewNode, 1)))
			goto clean;
		NewNode->mData = GS_ARGOWN(&Tree);
		NewNode->mNext = *NodeList;
		*NodeList = GS_ARGOWN(&NewNode);
	}

clean:
	GS_DELETE_F(&NewNode, gs_tree_inflated_node_destroy);
	GS_DELETE_F(&Tree, gs_tree_inflated_destroy);

	return r;
}

/* takes ownership of 'Tree' */
int tree_toposort_2(const char *RepositoryPathBuf, size_t LenRepositoryPath, struct GsTreeInflated *Tree, struct GsTreeInflatedNode **oNodeList)
{
	/* https://en.wikipedia.org/wiki/Topological_sorting#Depth-first_search */
	int r = 0;
	toposet_t MarkSet;
	struct GsTreeInflatedNode *NodeList = NULL;
	if (!!(r = tree_toposort_visit2(RepositoryPathBuf, LenRepositoryPath, &MarkSet, &NodeList, GS_ARGOWN(&Tree))))
		goto clean;

	if (oNodeList)
		*oNodeList = GS_ARGOWN(&NodeList);

clean:
	GS_DELETE_F(&Tree, gs_tree_inflated_destroy);
	GS_DELETE_F(&NodeList, gs_tree_inflated_node_list_destroy);

	return r;
}

int gs_tree_inflated_create(struct GsTreeInflated **oTree)
{
	if (!(*oTree = (struct GsTreeInflated *) malloc(sizeof **oTree)))
		return 1;
	(*oTree) = {};
	return 0;
}

int gs_tree_inflated_destroy(struct GsTreeInflated *ioTree)
{
	if (ioTree) {
		if (ioTree->mDataBuf) {
			free((void *) ioTree->mDataBuf);
			ioTree->mDataBuf = NULL;
		}
		if (ioTree->mDeflatedBuf) {
			free((void *) ioTree->mDeflatedBuf);
			ioTree->mDeflatedBuf = NULL;
		}
		free(ioTree);
	}
	return 0;
}

int gs_tree_inflated_destroy_dataonly(struct GsTreeInflated *ioTree)
{
	if (ioTree) {
		if (ioTree->mDataBuf) {
			free((void *) ioTree->mDataBuf);
			ioTree->mDataBuf = NULL;
		}
	}
	return 0;
}

int gs_tree_inflated_node_destroy(struct GsTreeInflatedNode *ioNode)
{
	if (ioNode) {
		GS_DELETE_F(&ioNode->mData, gs_tree_inflated_destroy);
		free((void *) ioNode);
	}
	return 0;
}

int gs_tree_inflated_node_list_destroy(struct GsTreeInflatedNode *ioHead)
{
	for (struct GsTreeInflatedNode *it = ioHead; it; /*dummy*/) {
		struct GsTreeInflatedNode *tmp = it;
		it = tmp->mNext;
		GS_DELETE_F(&tmp, gs_tree_inflated_node_destroy);
	}
	return 0;
}

int gs_tree_inflated_node_list_reverse(struct GsTreeInflatedNode **List)
{
	/* can not fail - leaks or whatever on failure */
	struct GsTreeInflatedNode *Head = *List;
	if (Head != NULL) {
		struct GsTreeInflatedNode *Rev = Head;
		struct GsTreeInflatedNode *Nxt = Head->mNext;
		Rev->mNext = NULL;  /* first node (Head) becomes last of Rev and is cut off here */
		while (Nxt) {
			struct GsTreeInflatedNode *Tmp = Nxt;
			Nxt = Nxt->mNext;

			Tmp->mNext = Rev;
			Rev = Tmp;
		}
		*List = Rev;
	}
	return 0;
}

int gs_tree_inflated_vec_serialize(
	const GsTreeInflated **TreeListVec, size_t NumTreeList,
	std::string *oSizeBuffer, std::string *oObjectBuffer)
{
	int r = 0;

	size_t ObjectCumulativeSize = 0;
	std::string SizeBuffer;
	std::string ObjectBuffer;

	for (uint32_t i = 0; i < NumTreeList; i++)
		ObjectCumulativeSize += TreeListVec[i]->mLenDeflated;

	SizeBuffer.reserve(NumTreeList * sizeof(uint32_t));
	ObjectBuffer.reserve(ObjectCumulativeSize);

	for (uint32_t i = 0; i < NumTreeList; i++) {
		char sizebuf[sizeof(uint32_t)] = {};
		aux_uint32_to_LE(TreeListVec[i]->mLenDeflated, sizebuf, sizeof sizebuf);
		SizeBuffer.append(sizebuf,
			sizeof sizebuf);
		ObjectBuffer.append(TreeListVec[i]->mDeflatedBuf,
			TreeListVec[i]->mLenDeflated);
	}

	if (oSizeBuffer)
		oSizeBuffer->swap(SizeBuffer);

	if (oObjectBuffer)
		oObjectBuffer->swap(ObjectBuffer);

clean:

	return r;
}

int aux_gittest_init() {
	git_libgit2_init();
	return 0;
}

void aux_uint32_to_LE(uint32_t a, char *oBuf, size_t bufsize) {
	GS_ASSERT(sizeof(uint32_t) == 4 && bufsize == 4);
	oBuf[0] = (a >> 0) & 0xFF;
	oBuf[1] = (a >> 8) & 0xFF;
	oBuf[2] = (a >> 16) & 0xFF;
	oBuf[3] = (a >> 24) & 0xFF;
}

void aux_LE_to_uint32(uint32_t *oA, const char *buf, size_t bufsize) {
	GS_ASSERT(sizeof(uint32_t) == 4 && bufsize == 4);
	uint32_t w = 0;
	w |= (buf[0] & 0xFF) << 0;
	w |= (buf[1] & 0xFF) << 8;
	w |= (buf[2] & 0xFF) << 16;
	w |= (buf[3] & 0xFF) << 24;
	*oA = w;
}

void aux_topolist_print(const topolist_t &NodeListTopo) {
	for (topolist_t::const_iterator it = NodeListTopo.begin(); it != NodeListTopo.end(); it++) {
		char buf[GIT_OID_HEXSZ] = {};
		git_oid_fmt(buf, git_tree_id(*it));
		GS_LOG(I, PF, "tree [%.*s]", sizeof buf, buf);
	}
}

int aux_oid_tree_blob_byname(git_repository *Repository, git_oid *TreeOid, const char *WantedBlobName, git_oid *oBlobOid) {
	int r = 0;

	git_tree *Tree = NULL;

	const git_tree_entry *Entry = NULL;
	git_otype EntryType = GIT_OBJ_BAD;
	const git_oid *BlobOid = NULL;

	if (!!(r = git_tree_lookup(&Tree, Repository, TreeOid)))
		goto clean;

	Entry = git_tree_entry_byname(Tree, WantedBlobName);

	if (! Entry)
		{ r = 1; goto clean; }

	EntryType = git_tree_entry_type(Entry);

	if (EntryType != GIT_OBJ_BLOB)
		{ r = 1; goto clean; }

	BlobOid = git_tree_entry_id(Entry);

	if (oBlobOid)
		git_oid_cpy(oBlobOid, BlobOid);

clean:
	if (Tree)
		git_tree_free(Tree);

	return r;
}

int aux_oid_latest_commit_tree(git_repository *Repository, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid) {
	/* return value GIT_ENOTFOUND is part of the API for this function */

	int r = 0;
	int errC = 0;

	git_oid CommitHeadOid = {};
	git_commit *CommitHead = NULL;
	git_tree *TreeHead = NULL;

	if (!!(r = git_reference_name_to_id(&CommitHeadOid, Repository, RefName)))
		goto clean;

	// FIXME: not sure if GIT_ENOTFOUND return counts as official API for git_commit_lookup
	errC = git_commit_lookup(&CommitHead, Repository, &CommitHeadOid);
	if (!!errC && errC != GIT_ENOTFOUND)
		{ r = errC; goto clean; }

	if (!!(r = git_commit_tree(&TreeHead, CommitHead)))
		goto clean;

	if (oCommitHeadOid)
		git_oid_cpy(oCommitHeadOid, &CommitHeadOid);

	if (oTreeHeadOid)
		git_oid_cpy(oTreeHeadOid, git_tree_id(TreeHead));

clean:
	if (TreeHead)
		git_tree_free(TreeHead);

	if (CommitHead)
		git_commit_free(CommitHead);

	return r;
}

int serv_latest_commit_tree_oid(git_repository *Repository, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid) {
	return aux_oid_latest_commit_tree(Repository, RefName, oCommitHeadOid, oTreeHeadOid);
}

int clnt_latest_commit_tree_oid(git_repository *RepositoryT, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid) {
	/* if the latest commit is not found, return success, setting output oids to zero */

	int r = 0;

	git_oid CommitHeadOid = {};
	git_oid TreeHeadOid = {};

	git_oid OidZero = {};
	GS_ASSERT(!!git_oid_iszero(&OidZero));

	int errX = aux_oid_latest_commit_tree(RepositoryT, RefName, &CommitHeadOid, &TreeHeadOid);
	if (!!errX && errX != GIT_ENOTFOUND)
	{
		r = errX; goto clean;
	}
	GS_ASSERT(errX == 0 || errX == GIT_ENOTFOUND);
	if (errX == GIT_ENOTFOUND) {
		git_oid_cpy(&CommitHeadOid, &OidZero);
		git_oid_cpy(&TreeHeadOid, &OidZero);
	}

	if (oCommitHeadOid)
		git_oid_cpy(oCommitHeadOid, &CommitHeadOid);

	if (oTreeHeadOid)
		git_oid_cpy(oTreeHeadOid, &TreeHeadOid);

clean:

	return r;
}

int serv_oid_treelist(git_repository *Repository, git_oid *TreeOid, std::vector<git_oid> *oOutput) {
	int r = 0;

	git_tree *Tree = NULL;
	
	topolist_t NodeListTopo;
	std::vector<git_oid> Output;

	int OutputIdx = 0;

	if (!!(r = git_tree_lookup(&Tree, Repository, TreeOid)))
		goto clean;

	if (!!(r = tree_toposort(Repository, Tree, &NodeListTopo)))
		goto clean;

	/* output in reverse topological order */
	Output.resize(NodeListTopo.size());  // FIXME: inefficient list size operation?
	for (topolist_t::reverse_iterator it = NodeListTopo.rbegin(); it != NodeListTopo.rend(); OutputIdx++, it++)
		git_oid_cpy(Output.data() + OutputIdx,  git_tree_id(*it));

	if (oOutput)
		oOutput->swap(Output);

clean:
	if (Tree)
		git_tree_free(Tree);

	return r;
}

int aux_serialize_objects(
	git_repository *Repository, std::vector<git_oid> *ObjectOid, git_otype ObjectWantedType,
	std::string *oSizeBuffer, std::string *oObjectBuffer)
{
	int r = 0;

	git_odb *Odb = NULL;

	std::vector<git_odb_object *> Object;
	std::vector<uint32_t>         ObjectSize;
	size_t ObjectCumulativeSize = 0;
	std::string SizeBuffer;
	std::string ObjectBuffer;

	if (!!(r = git_repository_odb(&Odb, Repository)))
		goto clean;

	Object.resize(ObjectOid->size());
	for (uint32_t i = 0; i < ObjectOid->size(); i++) {
		if (!!(r = git_odb_read(&Object[i], Odb, ObjectOid->data() + i)))
			goto clean;
		if (git_odb_object_type(Object[i]) != ObjectWantedType)
			{ r = 1; goto clean; }
	}

	ObjectSize.resize(ObjectOid->size());
	for (uint32_t i = 0; i < ObjectOid->size(); i++) {
		ObjectSize[i] = (uint32_t)git_odb_object_size(Object[i]);
		ObjectCumulativeSize += ObjectSize[i];
	}

	GS_ASSERT(sizeof(uint32_t) == 4);
	SizeBuffer.reserve(Object.size() * sizeof(uint32_t));
	ObjectBuffer.reserve(ObjectCumulativeSize);
	for (uint32_t i = 0; i < Object.size(); i++) {
		char sizebuf[sizeof(uint32_t)] = {};
		aux_uint32_to_LE(ObjectSize[i], sizebuf, sizeof sizebuf);
		SizeBuffer.append(sizebuf,
			sizeof sizebuf);
		ObjectBuffer.append(static_cast<const char *>(git_odb_object_data(Object[i])),
			ObjectSize[i]);
	}

	if (oSizeBuffer)
		oSizeBuffer->swap(SizeBuffer);

	if (oObjectBuffer)
		oObjectBuffer->swap(ObjectBuffer);

clean:
	for (uint32_t i = 0; i < Object.size(); i++)
		if (Object[i])
			git_odb_object_free(Object[i]);

	if (Odb)
		git_odb_free(Odb);

	return r;
}

int serv_serialize_trees(git_repository *Repository, std::vector<git_oid> *TreeOid, std::string *oSizeBuffer, std::string *oTreeBuffer) {
	return aux_serialize_objects(Repository, TreeOid, GIT_OBJ_TREE, oSizeBuffer, oTreeBuffer);
}

int serv_serialize_blobs(git_repository *Repository, std::vector<git_oid> *BlobOid, std::string *oSizeBuffer, std::string *oBlobBuffer) {
	return aux_serialize_objects(Repository, BlobOid, GIT_OBJ_BLOB, oSizeBuffer, oBlobBuffer);
}

int aux_deserialize_sizebuffer(uint8_t *DataStart, uint32_t DataLength, uint32_t OffsetSizeBuffer, uint32_t SizeVecLen, std::vector<uint32_t> *oSizeVector, size_t *oCumulativeSize) {
	int r = 0;

	std::vector<uint32_t> SizeVector;
	size_t CumulativeSize = 0;

	GS_ASSERT(DataStart + OffsetSizeBuffer + SizeVecLen * sizeof(uint32_t) <= DataStart + DataLength);

	SizeVector.resize(SizeVecLen);
	for (uint32_t i = 0; i < SizeVecLen; i++) {
		aux_LE_to_uint32(&SizeVector[i], (char *)(DataStart + OffsetSizeBuffer + i * sizeof(uint32_t)), 4);
		CumulativeSize += SizeVector[i];
	}

	if (oSizeVector)
		oSizeVector->swap(SizeVector);
	if (oCumulativeSize)
		*oCumulativeSize = CumulativeSize;

clean:

	return r;
}

int aux_deserialize_object(
	git_repository *RepositoryT,
	const char *DataBuf, size_t LenData,
	git_otype Type,
	git_oid *CheckOidOpt)
{
	int r = 0;

	git_odb *OdbT = NULL;
	git_oid WrittenOid = {};

	if (!!(r = git_repository_odb(&OdbT, RepositoryT)))
		goto clean;

	if (!!(r = git_odb_write(&WrittenOid, OdbT, DataBuf, LenData, Type)))
		GS_GOTO_CLEAN();

	if (CheckOidOpt)
		if (!!(r = git_oid_cmp(CheckOidOpt, &WrittenOid)))
			GS_GOTO_CLEAN();

clean:
	git_odb_free(OdbT);

	return r;
}

int aux_deserialize_objects(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, git_otype WrittenObjectType, std::vector<git_oid> *oWrittenObjectOid)
{
	int r = 0;

	git_odb *OdbT = NULL;

	std::vector<git_oid> WrittenObjectOid;
	std::vector<uint32_t> SizeVector;
	size_t CumulativeSize = 0;

	if (!!(r = git_repository_odb(&OdbT, RepositoryT)))
		goto clean;

	if (!!(r = aux_deserialize_sizebuffer(DataStartSizeBuffer, DataLengthSizeBuffer, OffsetSizeBuffer, PairedVecLen, &SizeVector, &CumulativeSize)))
		goto clean;

	GS_ASSERT(DataStartObjectBuffer + OffsetObjectBuffer + CumulativeSize <= DataStartObjectBuffer + DataLengthObjectBuffer);

	WrittenObjectOid.resize(SizeVector.size());
	for (uint32_t idx = 0, i = 0; i < SizeVector.size(); idx+=SizeVector[i], i++) {
		git_oid FreshOid = {};
		/* NOTE: supposedly git_odb_stream_write recommended */
		if (!!(r = git_odb_write(&FreshOid, OdbT, DataStartObjectBuffer + OffsetObjectBuffer + idx, SizeVector[i], WrittenObjectType)))
			goto clean;
		git_oid_cpy(&WrittenObjectOid[i], &FreshOid);
	}

	if (oWrittenObjectOid)
		oWrittenObjectOid->swap(WrittenObjectOid);

clean:
	if (OdbT)
		git_odb_free(OdbT);

	return r;
}

int aux_clnt_deserialize_trees(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedTree)
{
	return aux_deserialize_objects(RepositoryT,
		DataStartSizeBuffer, DataLengthSizeBuffer, OffsetSizeBuffer,
		DataStartObjectBuffer, DataLengthObjectBuffer, OffsetObjectBuffer,
		PairedVecLen, GIT_OBJ_TREE, oDeserializedTree);
}

int clnt_deserialize_trees(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedTree)
{
	return aux_deserialize_objects(
		RepositoryT,
		DataStartSizeBuffer, DataLengthSizeBuffer, OffsetSizeBuffer,
		DataStartObjectBuffer, DataLengthObjectBuffer, OffsetObjectBuffer,
		PairedVecLen, GIT_OBJ_TREE, oDeserializedTree);
}

int clnt_deserialize_blobs(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedBlob)
{
	return aux_deserialize_objects(
		RepositoryT,
		DataStartSizeBuffer, DataLengthSizeBuffer, OffsetSizeBuffer,
		DataStartObjectBuffer, DataLengthObjectBuffer, OffsetObjectBuffer,
		PairedVecLen, GIT_OBJ_BLOB, oDeserializedBlob);
}

int clnt_missing_trees(git_repository *RepositoryT, std::vector<git_oid> *Treelist, std::vector<git_oid> *oMissingTreeList) {
	int r = 0;

	std::vector<git_oid> MissingTree;
	std::vector<git_tree *> TmpTree;

	TmpTree.resize(Treelist->size());
	for (uint32_t i = 0; i < Treelist->size(); i++) {
		// FIXME: not sure if GIT_ENOUTFOUND return counts as official API for git_tree_lookup
		int errTree = git_tree_lookup(&TmpTree[i], RepositoryT, &(*Treelist)[i]);
		if (errTree != 0 && errTree != GIT_ENOTFOUND)
			{ r = 1; goto clean; }
		if (errTree == 0)
			continue;
		GS_ASSERT(errTree == GIT_ENOTFOUND);
		MissingTree.push_back((*Treelist)[i]);
	}

	if (oMissingTreeList)
		oMissingTreeList->swap(MissingTree);

clean:
	for (uint32_t i = 0; i < TmpTree.size(); i++)
		if (TmpTree[i])
			git_tree_free(TmpTree[i]);

	return r;
}

int aux_memory_repository_new(git_repository **oRepositoryMemory) {
	int r = 0;

	git_repository *RepositoryMemory = NULL;
	git_odb_backend *BackendMemory = NULL;
	git_odb *RepositoryOdb = NULL;

	/* https://github.com/libgit2/libgit2/blob/master/include/git2/sys/repository.h */
	if (!!(r = git_repository_new(&RepositoryMemory)))
		goto clean;

	/* https://github.com/libgit2/libgit2/blob/master/include/git2/sys/mempack.h */

	if (!!(r = git_mempack_new(&BackendMemory)))
		goto clean;

	if (!!(r = git_repository_odb(&RepositoryOdb, RepositoryMemory)))
		goto clean;

	if (!!(r = git_odb_add_backend(RepositoryOdb, BackendMemory, 999)))
		goto clean;

	if (oRepositoryMemory)
		*oRepositoryMemory = RepositoryMemory;

clean:
	/* NOTE: backend is owned by odb, and odb is owned by repository.
	     backend thus destroyed indirectly with the repository. */

	if (RepositoryOdb)
		git_odb_free(RepositoryOdb);

	if (!!r) {
		if (RepositoryMemory)
			git_repository_free(RepositoryMemory);
	}

	return r;
}

int aux_clnt_dual_lookup_expect_missing(
	git_repository *RepositoryMemory, git_repository *RepositoryT,
	git_oid *TreeOid,
	git_tree **oTreeMem, git_tree **oTreeT)
{
	/*
	* meant for use while processing missing trees,
	* where trees are first inserted into a memory backend-ed repository.
	* therefore tree is expected to be missing in the other repository.
	*/

	int r = 0;

	git_tree *TreeMem = NULL;
	git_tree *TreeT   = NULL;

	int errM = git_tree_lookup(&TreeMem, RepositoryMemory, TreeOid);
	int errT = git_tree_lookup(&TreeT, RepositoryT, TreeOid);
	if (!!errM)  // should have been inserted into memory repository. must be present.
		{ r = 1; goto clean; }
	if (errT != GIT_ENOTFOUND)  // must be non-present (missing tree)
		{ r = 1; goto clean; }
	GS_ASSERT(errM == 0 && errT == GIT_ENOTFOUND);

	if (oTreeMem)
		*oTreeMem = TreeMem;
	if (oTreeT)
		*oTreeT = TreeT;

clean:
	if (!!r) {
		if (TreeT)
			git_tree_free(TreeT);

		if (TreeMem)
			git_tree_free(TreeMem);
	}

	return r;
}

int clnt_missing_blobs_bare(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oMissingBloblist)
{
	int r = 0;

	std::vector<git_oid> DeserializedTree;
	std::vector<std::pair<git_tree *, git_tree *> > TreeMem_TreeT;
	std::vector<git_oid> MissingBloblist;

	git_repository *RepositoryMemory = NULL;
	git_odb *RepositoryTOdb = NULL;

	git_oid OidZero = {};
	GS_ASSERT(!!git_oid_iszero(&OidZero));

	if (!!(r = aux_memory_repository_new(&RepositoryMemory)))
		goto clean;

	if (!!(r = git_repository_odb(&RepositoryTOdb, RepositoryT)))
		goto clean;

	if (!!(r = aux_clnt_deserialize_trees(
		RepositoryMemory,
		DataStartSizeBuffer, DataLengthSizeBuffer, OffsetSizeBuffer,
		DataStartObjectBuffer, DataLengthObjectBuffer, OffsetObjectBuffer,
		PairedVecLen, &DeserializedTree)))
	{
		goto clean;
	}

	TreeMem_TreeT.resize(DeserializedTree.size());
	for (uint32_t i = 0; i < DeserializedTree.size(); i++) {
		if (!!(r = aux_clnt_dual_lookup_expect_missing(RepositoryMemory, RepositoryT, &DeserializedTree[i],
			&TreeMem_TreeT[i].first, &TreeMem_TreeT[i].second)))
		{
			goto clean;
		}
		size_t entrycount = git_tree_entrycount(TreeMem_TreeT[i].first);
		for (uint32_t j = 0; j < entrycount; j++) {
			const git_tree_entry *EntryMemory = git_tree_entry_byindex(TreeMem_TreeT[i].first, j);
			const git_oid *EntryMemoryOid = git_tree_entry_id(EntryMemory);
			const git_otype EntryMemoryType = git_tree_entry_type(EntryMemory);
			if (EntryMemoryType == GIT_OBJ_TREE)
				continue;
			GS_ASSERT(EntryMemoryType == GIT_OBJ_BLOB);
			if (git_odb_exists(RepositoryTOdb, EntryMemoryOid))
				continue;
			MissingBloblist.push_back(OidZero);
			git_oid_cpy(&MissingBloblist[MissingBloblist.size() - 1], EntryMemoryOid);
		}
	}

	if (oMissingBloblist)
		oMissingBloblist->swap(MissingBloblist);

clean:
	for (uint32_t i = 0; i < TreeMem_TreeT.size(); i++) {
		if (TreeMem_TreeT[i].first)
			git_tree_free(TreeMem_TreeT[i].first);
		if (TreeMem_TreeT[i].second)
			git_tree_free(TreeMem_TreeT[i].second);
	}

	if (RepositoryTOdb)
		git_odb_free(RepositoryTOdb);

	if (RepositoryMemory)
		git_repository_free(RepositoryMemory);

	return r;
}

int clnt_missing_blobs(git_repository *RepositoryT, uint32_t PairedVecLen, std::string *SizeBuffer, std::string *TreeBuffer, std::vector<git_oid> *oMissingBloblist) {
	return clnt_missing_blobs_bare(
		RepositoryT,
		(uint8_t *)SizeBuffer->data(), SizeBuffer->size(), 0,
		(uint8_t *)TreeBuffer->data(), TreeBuffer->size(), 0,
		PairedVecLen, oMissingBloblist);
}

int aux_commit_buffer_checkexist_dummy(git_odb *OdbT, git_buf *CommitBuf, uint32_t *oExists, git_oid *oCommitOid) {
	int r = 0;

	git_oid CommitOid = {};

	uint32_t Exists = 0;

	if (!!(r = git_odb_hash(&CommitOid, CommitBuf->ptr, CommitBuf->size, GIT_OBJ_COMMIT)))
		goto clean;

	Exists = git_odb_exists(OdbT, &CommitOid);

	if (oExists)
		*oExists = Exists;

	if (oCommitOid)
		git_oid_cpy(oCommitOid, &CommitOid);

clean:

	return r;
}

int aux_commit_buffer_dummy(git_repository *RepositoryT, git_oid *TreeOid, git_buf *oCommitBuf) {
	int r = 0;

	git_tree *Tree = NULL;
	git_signature *Signature = NULL;
	git_buf CommitBuf = {};

	if (!!(r = git_tree_lookup(&Tree, RepositoryT, TreeOid)))
		goto clean;

	if (!!(r = git_signature_new(&Signature, "DummyName", "DummyEMail", 0, 0)))
		goto clean;
	
	if (!!(r = git_commit_create_buffer(&CommitBuf, RepositoryT, Signature, Signature, "UTF-8", "Dummy", Tree, 0, NULL)))
		goto clean;

	if (oCommitBuf)
		*oCommitBuf = CommitBuf;

clean:
	if (!!r) {
		git_buf_free(&CommitBuf);
	}

	if (Signature)
		git_signature_free(Signature);

	if (Tree)
		git_tree_free(Tree);

	return r;
}

int aux_commit_commit_dummy(git_odb *OdbT, git_buf *CommitBuf, git_oid *oCommitOid) {
	int r = 0;

	git_oid CommitOid = {};

	if (!!(r = git_odb_write(&CommitOid, OdbT, CommitBuf->ptr, CommitBuf->size, GIT_OBJ_COMMIT)))
		goto clean;

	if (oCommitOid)
		git_oid_cpy(oCommitOid, &CommitOid);

clean:

	return r;
}

/* WARNING: side-effects on Repository through git_repository_set_workdir
*/
int clnt_tree_ensure_from_workdir(
	git_repository *Repository,
	const char *DirBuf, size_t LenDir,
	git_oid *oTreeOid)
{
	int r = 0;

	const char *PathSpecAllC = "*";
	git_strarray PathSpecAll = {};
	PathSpecAll.count = 1;
	PathSpecAll.strings = (char **) &PathSpecAllC;

	git_index *Index = NULL;

	git_oid TreeOid = {};

	if (!!(r = gs_buf_ensure_haszero(DirBuf, LenDir + 1)))
		GS_GOTO_CLEAN();

	if (!!(r = git_repository_set_workdir(Repository, DirBuf, 0)))
		GS_GOTO_CLEAN();

	if (!!(r = git_repository_index(&Index, Repository)))
		GS_GOTO_CLEAN();

	if (!!(r = git_index_clear(Index)))
		GS_GOTO_CLEAN();

	if (!!(r = git_index_add_all(Index, &PathSpecAll, GIT_INDEX_ADD_FORCE, NULL, NULL)))
		GS_GOTO_CLEAN();

	if (!!(r = git_index_write_tree_to(&TreeOid, Index, Repository)))
		GS_GOTO_CLEAN();

	if (oTreeOid)
		*oTreeOid = TreeOid;

clean:
	git_index_free(Index);

	return r;
}

int clnt_tree_ensure_single(git_repository *Repository,
	const char *SingleBlobNameBuf, size_t LenSingleBlobName,
	git_oid *BlobOid,
	git_oid *oTreeOid)
{
	int r = 0;

	git_treebuilder *TreeBuilder = NULL;

	git_oid TreeOid = {};

	if (!!(r = gs_buf_ensure_haszero(SingleBlobNameBuf, LenSingleBlobName + 1)))
		GS_GOTO_CLEAN();

	if (!!(r = git_treebuilder_new(&TreeBuilder, Repository, NULL)))
		GS_GOTO_CLEAN();

	// FIXME: really GIT_FILEMODE_BLOB_EXECUTABLE? makes sense but what about just GIT_FILEMODE_BLOB?
	if (!!(r = git_treebuilder_insert(NULL, TreeBuilder, SingleBlobNameBuf, BlobOid, GIT_FILEMODE_BLOB_EXECUTABLE)))
		GS_GOTO_CLEAN();

	if (!!(r = git_treebuilder_write(&TreeOid, TreeBuilder)))
		GS_GOTO_CLEAN();

	if (oTreeOid)
		*oTreeOid = TreeOid;

clean:
	git_treebuilder_free(TreeBuilder);

	return r;
}

int clnt_tree_ensure_dummy(git_repository *Repository, git_oid *oTreeOid)
{
	int r = 0;

	char DummyData[] = "Hello World";
	char DummyFileName[] = "DummyFileName.txt";

	git_oid BlobOid = {};
	git_oid TreeOid = {};
	git_oid CommitOid = {};

	if (!!(r = git_blob_create_frombuffer(&BlobOid, Repository, DummyData, sizeof DummyData - 1)))
		GS_GOTO_CLEAN();

	if (!!(r = clnt_tree_ensure_single(Repository, DummyFileName, sizeof DummyFileName - 1, &BlobOid, oTreeOid)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int clnt_commit_ensure_dummy(git_repository *RepositoryT, git_oid *TreeOid, git_oid *oCommitOid) {
	int r = 0;

	git_odb *OdbT = NULL;
	git_buf CommitBuf = {};
	uint32_t Exists = 0;
	git_oid CommitOid = {};

	if (!!(r = git_repository_odb(&OdbT, RepositoryT)))
		goto clean;

	if (!!(r = aux_commit_buffer_dummy(RepositoryT, TreeOid, &CommitBuf)))
		goto clean;

	if (!!(r = aux_commit_buffer_checkexist_dummy(OdbT, &CommitBuf, &Exists, &CommitOid)))
		goto clean;

	if (!Exists) {
		if (!!(r = aux_commit_commit_dummy(OdbT, &CommitBuf, &CommitOid)))
			goto clean;
	}

	if (oCommitOid)
		git_oid_cpy(oCommitOid, &CommitOid);

clean:
	git_buf_free(&CommitBuf);

	if (OdbT)
		git_odb_free(OdbT);

	return r;
}

int clnt_commit_setref(git_repository *RepositoryT, const char *RefName, git_oid *CommitOid) {
	int r = 0;

	git_reference *Reference = NULL;

	const char DummyLogMessage[] = "DummyLogMessage";

	int errC = git_reference_create(&Reference, RepositoryT, RefName, CommitOid, true, DummyLogMessage);
	if (!!errC && errC != GIT_EEXISTS)
		{ r = errC; goto clean; }
	GS_ASSERT(errC == 0 || errC == GIT_EEXISTS);
	/* if we are forcing creation (force=true), existing reference is fine and will be overwritten */

clean:
	if (Reference)
		git_reference_free(Reference);

	return r;
}

int gs_repo_init(const char *RepoPathBuf, size_t LenRepoPath, const char *OptPathSanityCheckLump)
{
	int r = 0;

	git_repository *Repository = NULL;
	int errR = 0;
	/* MKPATH for whole path creation (MKDIR only the last component) */
	int InitFlags = GIT_REPOSITORY_INIT_NO_REINIT | GIT_REPOSITORY_INIT_MKDIR | GIT_REPOSITORY_INIT_BARE;
	git_repository_init_options InitOptions = GIT_REPOSITORY_INIT_OPTIONS_INIT;
	
	GS_ASSERT(InitOptions.version == 1 && GIT_REPOSITORY_INIT_OPTIONS_VERSION == 1);

	if (!!(r = gs_buf_ensure_haszero(RepoPathBuf, LenRepoPath + 1)))
		GS_GOTO_CLEAN();

	if (OptPathSanityCheckLump && std::string(RepoPathBuf, LenRepoPath).find(OptPathSanityCheckLump) == std::string::npos)
		GS_ERR_CLEAN_L(1, E, PF, "Repository path suspicious [%.*s]", (int)LenRepoPath, RepoPathBuf);

	GS_LOG(I, PF, "Repository initializing [%.*s]", LenRepoPath, RepoPathBuf);

	InitOptions.flags = InitFlags;
	InitOptions.mode = GIT_REPOSITORY_INIT_SHARED_UMASK;
	InitOptions.workdir_path = NULL;
	InitOptions.description = NULL;
	InitOptions.template_path = NULL;
	InitOptions.initial_head = NULL;
	InitOptions.origin_url = NULL;

	errR = git_repository_init_ext(&Repository, RepoPathBuf, &InitOptions);
	if (!!errR && errR == GIT_EEXISTS)
		GS_ERR_NO_CLEAN_L(0, I, S, "Repository already exists");
	if (!!errR)
		GS_ERR_CLEAN(1);

noclean:

clean:
	if (Repository)
		git_repository_free(Repository);

	return r;
}

int aux_repository_open(
	const char *RepoOpenPathBuf, size_t LenRepoOpenPath,
	git_repository **oRepository)
{
	int r = 0;

	git_repository *Repository = NULL;

	if (!!(r = gs_buf_ensure_haszero(RepoOpenPathBuf, LenRepoOpenPath + 1)))
		goto clean;

	if (!!(r = git_repository_open(&Repository, RepoOpenPathBuf)))
		goto clean;

	if (oRepository)
		*oRepository = Repository;

clean:
	if (!!r) {
		if (Repository)
			git_repository_free(Repository);
	}

	return r;
}

int aux_repository_discover_open(const char *RepoDiscoverPath, git_repository **oRepository) {
	int r = 0;

	git_buf RepoPath = {};
	git_repository *Repository = NULL;

	if (!!(r = git_repository_discover(&RepoPath, RepoDiscoverPath, 0, NULL)))
		goto clean;

	if (!!(r = git_repository_open(&Repository, RepoPath.ptr)))
		goto clean;

	if (oRepository)
		*oRepository = Repository;

clean:
	if (!!r) {
		if (Repository)
			git_repository_free(Repository);
	}

	git_buf_free(&RepoPath);

	return r;
}

int aux_checkout(
	git_repository *Repository,
	git_oid *TreeOid,
	const char *CheckoutPathBuf, size_t LenCheckoutPath,
	const char *ExpectedContainsOptBuf, size_t LenExpectedContainsOpt)
{
	int r = 0;

	git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;

	git_object *TreeObject = NULL;

	if (!!(r = gs_buf_ensure_haszero(CheckoutPathBuf, LenCheckoutPath + 1)))
		goto clean;

	if (ExpectedContainsOptBuf) {
		if (!!(r = gs_buf_ensure_haszero(ExpectedContainsOptBuf, LenExpectedContainsOpt + 1)))
			goto clean;

		if (strstr(CheckoutPathBuf, ExpectedContainsOptBuf) == NULL)
			{ r = 1; goto clean; }
	}

	opts.checkout_strategy = 0;
	opts.checkout_strategy |= GIT_CHECKOUT_FORCE;
	// FIXME: want this flag but bugs have potential to cause more damage - enable after enough testing
	//opts.checkout_strategy |= GIT_CHECKOUT_REMOVE_UNTRACKED;

	opts.disable_filters = 1;
	opts.target_directory = CheckoutPathBuf;

	if (!!(r = git_object_lookup(&TreeObject, Repository, TreeOid, GIT_OBJ_TREE)))
		goto clean;

	if (!!(r = git_checkout_tree(Repository, TreeObject, &opts)))
		goto clean;

clean :
	if (TreeObject)
		git_object_free(TreeObject);

	return r;
}

int aux_repository_checkout(
	const char *RepoMasterUpdatePathBuf, size_t LenRepoMasterUpdatePath,
	const char *RefNameMainBuf, size_t LenRefNameMain,
	const char *RepoMasterUpdateCheckoutPathBuf, size_t LenRepoMasterUpdateCheckoutPath)
{
	int r = 0;

	git_repository *Repository = NULL;

	git_oid CommitHeadOid = {};
	git_oid TreeHeadOid = {};

	if (!!(r = gs_buf_ensure_haszero(RepoMasterUpdatePathBuf, LenRepoMasterUpdatePath + 1)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_repository_open(RepoMasterUpdatePathBuf, LenRepoMasterUpdatePath, &Repository)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_oid_latest_commit_tree(Repository, RefNameMainBuf, &CommitHeadOid, &TreeHeadOid)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_directory_create_unless_exist(RepoMasterUpdateCheckoutPathBuf, LenRepoMasterUpdateCheckoutPath)))
		GS_GOTO_CLEAN();

	// FIXME: hardcoded path check value (/data/)
	if (!!(r = aux_checkout(
		Repository,
		&TreeHeadOid,
		RepoMasterUpdateCheckoutPathBuf, LenRepoMasterUpdateCheckoutPath,
		NULL, 0)))
	{
		GS_GOTO_CLEAN();
	}

clean:
	if (Repository)
		git_repository_free(Repository);

	return r;
}

/** NOTE: 'Repository' must be the one pointed by 'RepoPathBuf'.
          'Repository' will be freed before performing actual maintenance.
		  Other git_repository-es should not be open from 'RepoPathBuf'
		  at the time of this call.
		  In other words Repository becomes owned by this function.
*/
int aux_repository_maintenance_special(
	git_repository *Repository, /*< owned */
	const char *RepoPathBuf, size_t LenRepoPath,
	const char *MaintenanceBkpPathBuf, size_t LenMaintenanceBkpPath)
{
	int r = 0;

	std::vector<git_oid> ReachableOid;

	GS_BYPART_DATA_VAR(OidVector, BypartReachableOid);
	GS_BYPART_DATA_INIT(OidVector, BypartReachableOid, &ReachableOid);

	if (!!(r = gs_reach_refs(Repository, gs_bypart_cb_OidVector, &BypartReachableOid)))
		GS_GOTO_CLEAN();

	std::sort(ReachableOid.begin(), ReachableOid.end(), oid_comparator_v_t());

	git_repository_free(GS_ARGOWN(&Repository));

	if (!!(r = git_memes_thunderdome(
		RepoPathBuf, LenRepoPath,
		ReachableOid.data(), ReachableOid.size(),
		MaintenanceBkpPathBuf, LenMaintenanceBkpPath,
		MaintenanceBkpPathBuf, LenMaintenanceBkpPath)))
	{
		GS_GOTO_CLEAN();
	}

clean:
	git_repository_free(Repository);

	return r;
}

int aux_objects_until_sizelimit(
	git_repository *Repository,
	const git_oid *Oid, size_t NumOid,
	size_t SoftSizeLimit,
	size_t *oNumUntilSizeLimit)
{
	int r = 0;

	git_odb *Odb = NULL;

	size_t i = 0;
	size_t SizeCumulative = 0;
	
	if (!!(r = git_repository_odb(&Odb, Repository)))
		goto clean;

	for (/*dummy*/; i < NumOid; i++) {
		size_t ObjLen = 0;
		git_otype ObjType = GIT_OBJ_BAD;
		if (!!(r = git_odb_read_header(&ObjLen, &ObjType, Odb, Oid + i)))
			goto clean;
		if (SizeCumulative + ObjLen >= SoftSizeLimit)
			break;
		SizeCumulative += ObjLen;
	}

	if (oNumUntilSizeLimit)
		*oNumUntilSizeLimit = i;

clean:
	git_odb_free(Odb);

	return r;
}

int gs_repo_compute_path(
	const char *HintPathBuf, size_t LenHintPath,
	char *ioRepoPathBuf, size_t RepoPathSize, size_t *oLenRepoPath)
{
	int r = 0;

	git_repository *Repository = NULL;
	const char *Tmp = NULL;
	size_t Siz = 0;

	if (!!(r = aux_repository_open(HintPathBuf, LenHintPath, &Repository)))
		GS_GOTO_CLEAN();

	if (!(Tmp = git_repository_path(Repository)))
		GS_ERR_CLEAN(1);
	Siz = strlen(Tmp);

	if (!!(r = gs_buf_copy_zero_terminate(Tmp, Siz, ioRepoPathBuf, RepoPathSize, oLenRepoPath)))
		GS_GOTO_CLEAN();

clean:
	git_repository_free(Repository);

	return r;
}

int gs_latest_commit_tree_oid(
	const char *RepositoryPathBuf, size_t LenRepositoryPath,
	const char *RefNameBuf, size_t LenRefName,
	git_oid *oCommitHeadOid, git_oid *oTreeHeadOid)
{
	int r = 0;

	char RefNameNormalBuf[1024] = {};
	char RefFilePathBuf[1024] = {};
	size_t LenRefFilePath = 0;
	char RefFileContentBuf[1024] = {};
	size_t LenRefFileContent = 0;

	git_oid CommitHeadOid = {};

	char CommitHeadPathBuf[1024] = {};
	size_t LenCommitHeadPath = 0;
	char CommitContentBuf[4096] = {};
	size_t LenCommitContent = 0;
	git_buf CommitInflated = {};
	git_otype CommitType = GIT_OBJ_BAD;
	size_t CommitOffset = 0;
	size_t CommitSize = 0;

	git_oid TreeHeadOid = {};


	/* https://github.com/git/git/blob/f06d47e7e0d9db709ee204ed13a8a7486149f494/refs.c#L36-100 */
	/* also libgit2 refs.c git_reference__normalize_name */

	if (!!(r = gs_buf_ensure_haszero(RefNameBuf, LenRefName + 1)))
		GS_GOTO_CLEAN();

	if (!!(r = git_reference_normalize_name(RefNameNormalBuf, sizeof RefNameNormalBuf, RefNameBuf, GIT_REF_FORMAT_NORMAL)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_path_append_abs_rel(
		RepositoryPathBuf, LenRepositoryPath,
		RefNameBuf, LenRefName,
		RefFilePathBuf, sizeof RefFilePathBuf, &LenRefFilePath)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = gs_file_read_tobuffer_block(
		RefFilePathBuf, LenRefFilePath,
		RefFileContentBuf, sizeof RefFileContentBuf, &LenRefFileContent)))
	{
		GS_GOTO_CLEAN();
	}

	if (LenRefFileContent < GIT_OID_HEXSZ)
		GS_ERR_CLEAN(1);

	if (!!(r = git_oid_fromstr(&CommitHeadOid, RefFileContentBuf)))
		GS_GOTO_CLEAN();

	if (!!(r = git_memes_objpath(
		RepositoryPathBuf, LenRepositoryPath,
		&CommitHeadOid,
		CommitHeadPathBuf, sizeof CommitHeadPathBuf, &LenCommitHeadPath)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = gs_file_read_tobuffer_block(
		CommitHeadPathBuf, LenCommitHeadPath,
		CommitContentBuf, sizeof CommitContentBuf, &LenCommitContent)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = git_memes_inflate(CommitContentBuf, LenCommitContent, &CommitInflated, &CommitType, &CommitOffset, &CommitSize)))
		GS_GOTO_CLEAN();

	if (CommitType != GIT_OBJ_COMMIT)
		GS_ERR_CLEAN(1);

	if (!!(r = git_memes_commit(CommitInflated.ptr + CommitOffset, CommitSize, &TreeHeadOid)))
		GS_GOTO_CLEAN();

	if (oCommitHeadOid)
		git_oid_cpy(oCommitHeadOid, &CommitHeadOid);
	if (oTreeHeadOid)
		git_oid_cpy(oTreeHeadOid, &TreeHeadOid);

clean:

	return r;
}

int gs_git_read_tree(
	const char *RepositoryPathBuf, size_t LenRepositoryPath,
	git_oid *TreeOid,
	size_t TreeRawSizeLimit,
	int AlsoFillOutDeflated,
	struct GsTreeInflated **oTree)
{
	int r = 0;

	struct GsTreeInflated *Tree = NULL;

	char TreePathBuf[1024] = {};
	size_t LenTreePath = 0;

	char *TreeContentAlloc = NULL;
	size_t TreeContentAllocSize = TreeRawSizeLimit;
	size_t LenTreeContentAlloc = 0;

	git_buf TreeInflated = {};
	git_otype TreeType = GIT_OBJ_BAD;
	size_t TreeOffset = 0;
	size_t TreeSize = 0;

	char *FinalAlloc = NULL;

	/* locate tree file (but will not know about existence until actually reading) */

	if (!!(r = git_memes_objpath(
		RepositoryPathBuf, LenRepositoryPath,
		TreeOid,
		TreePathBuf, sizeof TreePathBuf, &LenTreePath)))
	{
		GS_GOTO_CLEAN();
	}

	/* alloc max sized buf, read tree file, adjust alloc size to actual size */

	if (!(TreeContentAlloc = (char *)malloc(TreeContentAllocSize)))
		GS_ERR_CLEAN(1);

	if (!!(r = gs_file_read_tobuffer_block(
		TreePathBuf, LenTreePath,
		TreeContentAlloc, TreeContentAllocSize, &LenTreeContentAlloc)))
	{
		GS_GOTO_CLEAN();
	}

	if (!(TreeContentAlloc = (char *)gs_realloc(TreeContentAlloc, LenTreeContentAlloc)))
		GS_ERR_CLEAN(1);

	/* decompress */

	if (!!(r = git_memes_inflate(TreeContentAlloc, LenTreeContentAlloc, &TreeInflated, &TreeType, &TreeOffset, &TreeSize)))
		GS_GOTO_CLEAN();

	if (TreeType != GIT_OBJ_TREE)
		GS_ERR_CLEAN(1);

	/* copy decompressed data for purpose of output */

	if (!(FinalAlloc = (char *)malloc(TreeInflated.size)))
		GS_ERR_CLEAN(1);

	memcpy(FinalAlloc, TreeInflated.ptr, TreeInflated.size);

	/* output */

	if (!!(r = gs_tree_inflated_create(&Tree)))
		GS_GOTO_CLEAN();

	git_oid_cpy(&Tree->mOid, TreeOid);
	Tree->mDataBuf = GS_ARGOWN(&FinalAlloc);
	Tree->mLenData = TreeInflated.size;
	Tree->mTreeOffset = TreeOffset;
	Tree->mTreeSize = TreeSize;
	Tree->mDeflatedBuf = AlsoFillOutDeflated ? GS_ARGOWN(&TreeContentAlloc) : NULL;
	Tree->mLenDeflated = AlsoFillOutDeflated ? LenTreeContentAlloc : 0;

	if (oTree)
		*oTree = GS_ARGOWN(&Tree);

clean:
	if (FinalAlloc)
		free(FinalAlloc);
	if (TreeContentAlloc)
		free(TreeContentAlloc);
	git_buf_free(&TreeInflated);
	GS_DELETE_F(&Tree, gs_tree_inflated_destroy);

	return r;
}

int gs_git_tree_blob_byname(
	struct GsTreeInflated *Tree,
	const char *WantedBlobNameBuf, size_t LenWantedBlobName,
	git_oid *oBlobOid)
{
	int r = 0;

	size_t Offset = 0;
	unsigned long long Mode = 0;
	const char *FileName = NULL;
	size_t FileNameLen = 0;
	git_oid EntryOid = {};

	while (!(r = git_memes_tree(Tree->mDataBuf, Tree->mLenData, &Offset, &Mode, &FileName, &FileNameLen, &EntryOid)) && Offset != -1) {
		// FIXME: really GIT_FILEMODE_BLOB_EXECUTABLE? makes sense but what about just GIT_FILEMODE_BLOB?
		if (Mode != GIT_FILEMODE_BLOB_EXECUTABLE)
			continue;
		if (LenWantedBlobName == FileNameLen && ! strncmp(WantedBlobNameBuf, FileName, FileNameLen)) {
			git_oid_cpy(oBlobOid, &EntryOid);
			GS_ERR_NO_CLEAN(0);
		}
	}

	GS_ERR_CLEAN(1);

noclean:

clean:

	return r;
}

int gs_treelist(
	const char *RepositoryPathBuf, size_t LenRepositoryPath,
	git_oid *TreeOid,
	struct GsTreeInflatedNode **oTopoList)
{
	int r = 0;

	struct GsTreeInflated *Tree = NULL;
	struct GsTreeInflatedNode *NodeListTopo = NULL;

	int OutputIdx = 0;

	if (!!(r = gs_git_read_tree(RepositoryPathBuf, LenRepositoryPath, TreeOid, GS_FIXME_ARBITRARY_TREE_MAX_SIZE_LIMIT, true, &Tree)))
		goto clean;

	if (!!(r = tree_toposort_2(RepositoryPathBuf, LenRepositoryPath, GS_ARGOWN(&Tree), &NodeListTopo)))
		goto clean;

	/* output in reverse topological order */
	if (!!(r = gs_tree_inflated_node_list_reverse(&NodeListTopo)))
		goto clean;

	if (oTopoList)
		*oTopoList = GS_ARGOWN(&NodeListTopo);

clean:
	GS_DELETE_F(&NodeListTopo, gs_tree_inflated_node_list_destroy);
	GS_DELETE_F(&Tree, gs_tree_inflated_destroy);

	return r;
}

int stuff(
	const char *RefName, size_t LenRefName,
	const char *RepoOpenPath, size_t LenRepoOpenPath,
	const char *RepoTOpenPath, size_t LenRepoTOpenPath)
{
	int r = 0;

	git_buf RepoPath = {};
	git_repository *Repository = NULL;
	git_repository *RepositoryT = NULL;
	git_oid CommitHeadOid = {};
	git_oid TreeHeadOid = {};
	git_oid CommitHeadOidT = {};
	git_oid TreeHeadOidT = {};

	std::vector<git_oid> Treelist;
	std::vector<git_oid> MissingTreelist;

	std::string SizeBufferTree;
	std::string ObjectBufferTree;

	std::vector<git_oid> MissingBloblist;

	std::string SizeBufferBlob;
	std::string ObjectBufferBlob;

	std::vector<git_oid> WrittenTree;
	std::vector<git_oid> WrittenBlob;

	git_oid LastReverseToposortAkaFirstToposort = {};
	git_oid CreatedCommitOid = {};

	if (!!(r = aux_repository_open(RepoOpenPath, LenRepoOpenPath, &Repository)))
		goto clean;

	if (!!(r = aux_repository_open(RepoTOpenPath, LenRepoTOpenPath, &RepositoryT)))
		goto clean;

	if (!!(r = serv_latest_commit_tree_oid(Repository, RefName, &CommitHeadOid, &TreeHeadOid)))
		goto clean;

	if (!!(r = clnt_latest_commit_tree_oid(RepositoryT, RefName, &CommitHeadOidT, &TreeHeadOidT)))
		goto clean;

	if (git_oid_cmp(&TreeHeadOidT, &TreeHeadOid) == 0) {
		char buf[GIT_OID_HEXSZ] = {};
		git_oid_fmt(buf, &CommitHeadOid);
		GS_LOG(I, PF, "Have latest [%.*s]", (int)GIT_OID_HEXSZ, buf);
	}

	if (!!(r = serv_oid_treelist(Repository, &TreeHeadOid, &Treelist)))
		goto clean;

	if (!!(r = clnt_missing_trees(RepositoryT, &Treelist, &MissingTreelist)))
		goto clean;

	if (!!(r = serv_serialize_trees(Repository, &MissingTreelist, &SizeBufferTree, &ObjectBufferTree)))
		goto clean;

	if (!!(r = clnt_missing_blobs(RepositoryT, MissingTreelist.size(), &SizeBufferTree, &ObjectBufferTree, &MissingBloblist)))
		goto clean;

	if (!!(r = serv_serialize_blobs(Repository, &MissingBloblist, &SizeBufferBlob, &ObjectBufferBlob)))
		goto clean;

	if (!!(r = clnt_deserialize_trees(
		RepositoryT,
		(uint8_t *)SizeBufferTree.data(), SizeBufferTree.size(), 0,
		(uint8_t *)ObjectBufferTree.data(), ObjectBufferTree.size(), 0,
		MissingTreelist.size(), &WrittenTree)))
	{
		goto clean;
	}

	if (!!(r = clnt_deserialize_blobs(RepositoryT,
		(uint8_t *)SizeBufferBlob.data(), SizeBufferBlob.size(), 0,
		(uint8_t *)ObjectBufferBlob.data(), ObjectBufferBlob.size(), 0,
		MissingBloblist.size(), &WrittenBlob)))
	{
		goto clean;
	}

	GS_ASSERT(!Treelist.empty());
	git_oid_cpy(&LastReverseToposortAkaFirstToposort, &Treelist[Treelist.size() - 1]);
	if (!!(r = clnt_commit_ensure_dummy(RepositoryT, &LastReverseToposortAkaFirstToposort, &CreatedCommitOid)))
		goto clean;
	if (!!(r = clnt_commit_setref(RepositoryT, RefName, &CreatedCommitOid)))
		goto clean;

clean:
	if (RepositoryT)
		git_repository_free(RepositoryT);
	if (Repository)
		git_repository_free(Repository);

	return r;
}
