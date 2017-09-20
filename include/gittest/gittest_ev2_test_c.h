#ifndef _GITTEST_EV2_TEST_C_H_
#define _GITTEST_EV2_TEST_C_H_

#include <stdint.h>

#include <memory>
#include <vector>

#include <git2.h>

#include <gittest/gittest_ev2_test.h>

enum gs_clnt_state_code_t
{
	GS_CLNT_STATE_CODE_NEED_REPOSITORY = 0,
	GS_CLNT_STATE_CODE_NEED_TREE_HEAD = 1,
	GS_CLNT_STATE_CODE_NEED_TREELIST = 2,
	GS_CLNT_STATE_CODE_NEED_BLOBLIST = 3,
	GS_CLNT_STATE_CODE_NEED_WRITTEN_BLOB_AND_TREE = 4,
	GS_CLNT_STATE_CODE_NEED_UPDATED_REF = 5,
	GS_CLNT_STATE_CODE_NEED_NOTHING = 6,
	GS_CLNT_STATE_CODE_MAX_ENUM = 0x7FFFFFFF,
};

struct ClntState
{
	std::shared_ptr<git_repository *> mRepositoryT;

	std::shared_ptr<git_oid> mTreeHeadOid;

	std::shared_ptr<std::vector<git_oid> > mTreelist;
	std::shared_ptr<std::vector<git_oid> > mMissingTreelist;

	std::shared_ptr<std::vector<git_oid> >  mMissingBloblist;
	std::shared_ptr<GsPacketWithOffset> mTreePacketWithOffset;

	std::shared_ptr<std::vector<git_oid> > mWrittenBlob;
	std::shared_ptr<std::vector<git_oid> > mWrittenTree;

	std::shared_ptr<git_oid> mUpdatedRefOid;

	std::shared_ptr<std::vector<git_oid> > mReceivedOneShotBlob;
};

struct GsEvCtxClnt
{
	struct GsEvCtx base;
	struct GsAuxConfigCommonVars mCommonVars;
	struct ClntState *mClntState;
};

int clnt_state_make_default(struct ClntState *oState);
int clnt_state_code(struct ClntState *State, uint32_t *oCode);
int clnt_state_code_ensure(struct ClntState *State, uint32_t WantedCode);

#endif /* _GITTEST_EV2_TEST_C_H_ */
