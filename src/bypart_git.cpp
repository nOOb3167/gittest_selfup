#include <cstddef>
#include <cstdint>

#include <vector>

#include <git2.h>

#include <gittest/misc.h>
#include <gittest/bypart_git.h>

int gs_bypart_cb_OidVector(void *ctx, const char *d, int64_t l)
{
	int r = 0;

	git_oid Oid = {};
	GS_BYPART_DATA_VAR_CTX_NONUCF(OidVector, Data, ctx);

	GS_ASSERT(l == GIT_OID_RAWSZ);

	git_oid_fromraw(&Oid, (const unsigned char *)d);

	Data->m0OidVec->push_back(Oid);

clean:

	return r;
}

int gs_strided_for_oid_vec(
	const git_oid *OidVec,
	size_t OidVecNum,
	GsStrided *oStrided)
{
	int r = 0;

	uint8_t *DataStart = (uint8_t *) OidVec;
	uint32_t DataOffset = 0;
	uint32_t DataOffsetPlusOffset = DataOffset + offsetof(git_oid, id);
	uint32_t EltNum = OidVecNum;
	uint32_t EltSize = GIT_OID_RAWSZ;
	uint32_t EltStride = sizeof git_oid;

	GsStrided Strided = {
		DataStart,
		DataOffsetPlusOffset,
		EltNum,
		EltSize,
		EltStride,
	};

	uint32_t DataLength = OidVecNum * sizeof git_oid;

	if (EltSize > EltStride || DataOffset + EltStride * EltNum > DataLength)
		GS_ERR_CLEAN(1);

	if (oStrided)
		*oStrided = Strided;

clean:

	return r;
}

int gs_strided_for_oid_vec_cpp(std::vector<git_oid> *OidVec, GsStrided *oStrided) {
	int r = 0;

	uint8_t *DataStart = (uint8_t *)OidVec->data();
	uint32_t DataOffset = 0;
	uint32_t DataOffsetPlusOffset = DataOffset + offsetof(git_oid, id);
	uint32_t EltNum = OidVec->size();
	uint32_t EltSize = sizeof *OidVec->data();
	uint32_t EltStride = GIT_OID_RAWSZ;

	GsStrided Strided = {
		DataStart,
		DataOffsetPlusOffset,
		EltNum,
		EltSize,
		EltStride,
	};

	uint32_t DataLength = OidVec->size() * sizeof *OidVec->data();

	if (EltSize > EltStride || DataOffset + EltStride * EltNum > DataLength)
		GS_ERR_CLEAN(1);

	if (oStrided)
		*oStrided = Strided;

clean:

	return r;
}
