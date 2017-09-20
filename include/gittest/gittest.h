#ifndef _GITTEST_GITTEST_H_
#define _GITTEST_GITTEST_H_

#include <cstdint>

#include <vector>
#include <map>
#include <set>
#include <list>
#include <string>

#include <git2.h>

#include <gittest/misc.h>
#include <gittest/bypart.h>

#define GS_OID_STR_VAR(VARNAME) \
	char VARNAME ## Str [GIT_OID_HEXSZ + 1] = {};
#define GS_OID_STR_MAKE(VARNAME) \
	git_oid_nfmt((VARNAME ## Str), GIT_OID_HEXSZ + 1, (& VARNAME));

struct oid_comparator_t {
	bool operator()(const git_oid * const &a, const git_oid * const &b) {
		return git_oid_cmp(a, b) < 0;
	}
};

struct oid_comparator_v_t {
	bool operator()(const git_oid &a, const git_oid &b) {
		return git_oid_cmp(&a, &b) < 0;
	}
};

typedef ::std::set<const git_oid *, oid_comparator_t> toposet_t;
typedef ::std::list<git_tree *> topolist_t;

int gs_reach_refs(git_repository *Repository, gs_bypart_cb_t cb, void *ctx);

int tree_toposort_visit(git_repository *Repository, toposet_t *MarkSet, topolist_t *NodeList, git_tree *Tree);
int tree_toposort(git_repository *Repository, git_tree *Tree, topolist_t *oNodeList);

int aux_gittest_init();
void aux_uint32_to_LE(uint32_t a, char *oBuf, size_t bufsize);
void aux_LE_to_uint32(uint32_t *oA, const char *buf, size_t bufsize);
void aux_topolist_print(const topolist_t &NodeListTopo);
int aux_oid_tree_blob_byname(git_repository *Repository, git_oid *TreeOid, const char *WantedBlobName, git_oid *oBlobOid);
int aux_oid_latest_commit_tree(git_repository *Repository, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid);
int serv_latest_commit_tree_oid(git_repository *Repository, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid);
int clnt_latest_commit_tree_oid(git_repository *RepositoryT, const char *RefName, git_oid *oCommitHeadOid, git_oid *oTreeHeadOid);
int serv_oid_treelist(git_repository *Repository, git_oid *TreeOid, std::vector<git_oid> *oOutput);
int aux_serialize_objects(
	git_repository *Repository, std::vector<git_oid> *ObjectOid, git_otype ObjectWantedType,
	std::string *oSizeBuffer, std::string *oObjectBuffer);
int serv_serialize_trees(git_repository *Repository, std::vector<git_oid> *TreeOid, std::string *oSizeBuffer, std::string *oTreeBuffer);
int serv_serialize_blobs(git_repository *Repository, std::vector<git_oid> *BlobOid, std::string *oSizeBuffer, std::string *oBlobBuffer);
int aux_deserialize_sizebuffer(uint8_t *DataStart, uint32_t DataLength, uint32_t OffsetSizeBuffer, uint32_t SizeVecLen, std::vector<uint32_t> *oSizeVector, size_t *oCumulativeSize);
int aux_deserialize_object(
	git_repository *RepositoryT,
	const char *DataBuf, size_t LenData,
	git_otype Type,
	git_oid *CheckOidOpt);
int aux_deserialize_objects(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, git_otype WrittenObjectType, std::vector<git_oid> *oWrittenObjectOid);
int aux_clnt_deserialize_trees(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedTree);
int clnt_deserialize_trees(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedTree);
int clnt_deserialize_blobs(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oDeserializedBlob);
int clnt_missing_trees(git_repository *RepositoryT, std::vector<git_oid> *Treelist, std::vector<git_oid> *oMissingTreeList);
int aux_memory_repository_new(git_repository **oRepositoryMemory);
int aux_clnt_dual_lookup_expect_missing(
	git_repository *RepositoryMemory, git_repository *RepositoryT,
	git_oid *TreeOid,
	git_tree **oTreeMem, git_tree **oTreeT);
int clnt_missing_blobs_bare(
	git_repository *RepositoryT,
	uint8_t *DataStartSizeBuffer, uint32_t DataLengthSizeBuffer, uint32_t OffsetSizeBuffer,
	uint8_t *DataStartObjectBuffer, uint32_t DataLengthObjectBuffer, uint32_t OffsetObjectBuffer,
	uint32_t PairedVecLen, std::vector<git_oid> *oMissingBloblist);
int clnt_missing_blobs(git_repository *RepositoryT, uint32_t PairedVecLen, std::string *SizeBuffer, std::string *TreeBuffer, std::vector<git_oid> *oMissingBloblist);
int aux_commit_buffer_checkexist_dummy(git_odb *OdbT, git_buf *CommitBuf, uint32_t *oExists, git_oid *oCommitOid);
int aux_commit_buffer_dummy(git_repository *RepositoryT, git_oid *TreeOid, git_buf *oCommitBuf);
int aux_commit_commit_dummy(git_odb *OdbT, git_buf *CommitBuf, git_oid *oCommitOid);
int clnt_tree_ensure_from_workdir(
	git_repository *Repository,
	const char *DirBuf, size_t LenDir,
	git_oid *oTreeOid);
int clnt_tree_ensure_single(git_repository *Repository,
	const char *SingleBlobNameBuf, size_t LenSingleBlobName,
	git_oid *BlobOid,
	git_oid *oTreeOid);
int clnt_tree_ensure_dummy(git_repository *Repository, git_oid *oTreeOid);
int clnt_commit_ensure_dummy(git_repository *RepositoryT, git_oid *TreeOid, git_oid *oCommitOid);
int clnt_commit_setref(git_repository *RepositoryT, const char *RefName, git_oid *CommitOid);
int gs_repo_init(const char *RepoPathBuf, size_t LenRepoPath, const char *OptPathSanityCheckLump);
int aux_repository_open(
	const char *RepoOpenPathBuf, size_t LenRepoOpenPath,
	git_repository **oRepository);
int aux_repository_discover_open(const char *RepoDiscoverPath, git_repository **oRepository);
int aux_checkout(
	git_repository *Repository,
	git_oid *TreeOid,
	const char *CheckoutPathBuf, size_t LenCheckoutPath,
	const char *ExpectedContainsBuf, size_t LenExpectedContains);
int aux_repository_checkout(
	const char *RepoMasterUpdatePathBuf, size_t LenRepoMasterUpdatePath,
	const char *RefNameMainBuf, size_t LenRefNameMain,
	const char *RepoMasterUpdateCheckoutPathBuf, size_t LenRepoMasterUpdateCheckoutPath);
int aux_repository_maintenance_special(
	git_repository *Repository,
	const char *RepoPathBuf, size_t LenRepoPath,
	const char *MaintenanceBkpPathBuf, size_t LenMaintenanceBkpPath);
int aux_objects_until_sizelimit(
	git_repository *Repository,
	const git_oid *Oid, size_t NumOid,
	size_t SoftSizeLimit,
	size_t *oNumUntilSizeLimit);

int stuff(
	const char *RefName, size_t LenRefName,
	const char *RepoOpenPath, size_t LenRepoOpenPath,
	const char *RepoTOpenPath, size_t LenRepoTOpenPath);

#endif /* _GITTEST_GITTEST_H_ */
