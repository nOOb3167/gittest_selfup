#ifndef _GITTEST_BYPART_GIT_H_
#define _GITTEST_BYPART_GIT_H_

#include <stddef.h>

#include <git2.h>

#include <gittest/bypart.h>

/* GsBypartCbDataOidVector */
GS_BYPART_DATA_DECL(OidVector, std::vector<git_oid> *m0OidVec;);
#define GS_BYPART_TRIPWIRE_OidVector 0x23132358
#define GS_BYPART_DATA_INIT_OidVector(VARNAME, POIDVEC) (VARNAME).m0OidVec = POIDVEC;
int gs_bypart_cb_OidVector(void *ctx, const char *d, int64_t l);

int gs_strided_for_oid_vec(
	const git_oid *OidVec,
	size_t OidVecNum,
	GsStrided *oStrided);

int gs_strided_for_oid_vec_cpp(std::vector<git_oid> *OidVec, GsStrided *oStrided);

#endif /* _GITTEST_BYPART_GIT_H_ */
