#ifndef _GITTEST_FRAME_H_
#define _GITTEST_FRAME_H_

#include <stdint.h>

#include <gittest/bypart.h>

#define GS_FRAME_HEADER_STR_LEN 40
#define GS_FRAME_HEADER_NUM_LEN 4
#define GS_FRAME_HEADER_LEN (GS_FRAME_HEADER_STR_LEN + GS_FRAME_HEADER_NUM_LEN)
#define GS_FRAME_SIZE_LEN 4

#define GS_PAYLOAD_OID_LEN 20

#define GS_FRAME_TYPE_SERV_AUX_INTERRUPT_REQUESTED 0
#define GS_FRAME_TYPE_REQUEST_LATEST_COMMIT_TREE 1
#define GS_FRAME_TYPE_RESPONSE_LATEST_COMMIT_TREE 2
#define GS_FRAME_TYPE_REQUEST_TREELIST 3
#define GS_FRAME_TYPE_RESPONSE_TREELIST 4
#define GS_FRAME_TYPE_REQUEST_TREES 5
#define GS_FRAME_TYPE_RESPONSE_TREES 6
#define GS_FRAME_TYPE_REQUEST_BLOBS 7
#define GS_FRAME_TYPE_RESPONSE_BLOBS 8
#define GS_FRAME_TYPE_REQUEST_BLOBS_SELFUPDATE 9
#define GS_FRAME_TYPE_RESPONSE_BLOBS_SELFUPDATE 10
#define GS_FRAME_TYPE_REQUEST_LATEST_SELFUPDATE_BLOB 11
#define GS_FRAME_TYPE_RESPONSE_LATEST_SELFUPDATE_BLOB 12

#define GS_FRAME_TYPE_DECL2(name) GS_FRAME_TYPE_ ## name
#define GS_FRAME_TYPE_DECL(name) aux_frametype_make( # name , GS_FRAME_TYPE_DECL2(name) )

#define GS_FRAME_TYPE_DEFINE_FRAME_TYPE_ARRAY(VARNAME)       \
	GsFrameType (VARNAME)[] = {                              \
		GS_FRAME_TYPE_DECL(SERV_AUX_INTERRUPT_REQUESTED),    \
		GS_FRAME_TYPE_DECL(REQUEST_LATEST_COMMIT_TREE),      \
		GS_FRAME_TYPE_DECL(RESPONSE_LATEST_COMMIT_TREE),     \
		GS_FRAME_TYPE_DECL(REQUEST_TREELIST),                \
		GS_FRAME_TYPE_DECL(RESPONSE_TREELIST),               \
		GS_FRAME_TYPE_DECL(REQUEST_TREES),                   \
		GS_FRAME_TYPE_DECL(RESPONSE_TREES),                  \
		GS_FRAME_TYPE_DECL(REQUEST_BLOBS),                   \
		GS_FRAME_TYPE_DECL(RESPONSE_BLOBS),                  \
		GS_FRAME_TYPE_DECL(REQUEST_BLOBS_SELFUPDATE),        \
		GS_FRAME_TYPE_DECL(RESPONSE_BLOBS_SELFUPDATE),       \
		GS_FRAME_TYPE_DECL(REQUEST_LATEST_SELFUPDATE_BLOB),  \
		GS_FRAME_TYPE_DECL(RESPONSE_LATEST_SELFUPDATE_BLOB), \
	};                                                       \
	size_t Len ## VARNAME = sizeof (VARNAME) / sizeof *(VARNAME);


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/** value-struct
*/
struct GsFrameType {
	char mTypeName[GS_FRAME_HEADER_STR_LEN];
	uint32_t mTypeNum;
};

GsFrameType aux_frametype_make(const char *NameString, uint32_t NameNum);

bool aux_frametype_equals(const GsFrameType &a, const GsFrameType &b);

int aux_frame_enough_space(uint32_t TotalLength, uint32_t Offset, uint32_t WantedSpace);

int aux_frame_read_buf(
	uint8_t *DataStart, uint32_t DataLength, uint32_t DataOffset, uint32_t *DataOffsetNew,
	uint8_t *BufStart, uint32_t BufLength, uint32_t BufOffset, uint32_t NumToRead);
int aux_frame_write_buf(uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew, uint8_t *Buf, uint32_t BufLen);

int aux_frame_read_size(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t *oSize, uint32_t *oDataLengthLimit);
int aux_frame_write_size(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t Size);
int aux_frame_read_size_ensure(uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew, uint32_t MSize);
int aux_frame_read_size_limit(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t *oDataLengthLimit);

int aux_frame_read_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	GsFrameType *oFrameType);
int aux_frame_write_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	GsFrameType *FrameType);
int aux_frame_ensure_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	const GsFrameType &FrameType);

int aux_frame_read_oid(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint8_t *oOid, uint32_t OidSize);
int aux_frame_write_oid(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint8_t *Oid, uint32_t OidSize);
int aux_frame_read_oid_vec(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	void *ctx, gs_bypart_cb_t cb);
int aux_frame_read_oid_vec_(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_write_oid_vec(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	const GsStrided OidVec);

int aux_frame_full_aux_write_empty(
	GsFrameType *FrameType,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_aux_write_oid(
	GsFrameType *FrameType, uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_aux_write_oid_vec(
	GsFrameType *FrameType, const GsStrided OidVec,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_aux_read_paired_vec_noalloc(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t *oPairedVecLen, uint32_t *oOffsetSizeBuffer, uint32_t *oOffsetObjectBuffer);
int aux_frame_full_aux_write_paired_vec(
	GsFrameType *FrameType, uint32_t PairedVecLen,
	uint8_t *SizeBufferTreeData, uint32_t SizeBufferTreeSize,
	uint8_t *ObjectBufferTreeData, uint32_t ObjectBufferTreeSize,
	gs_bysize_cb_t cb, void *ctx);

int aux_frame_full_write_serv_aux_interrupt_requested(
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_latest_commit_tree(
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_response_latest_commit_tree(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_treelist(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_response_treelist(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_trees(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_response_trees(
	uint32_t PairedVecLen,
	uint8_t *SizeBufferTreeData, uint32_t SizeBufferTreeSize,
	uint8_t *ObjectBufferTreeData, uint32_t ObjectBufferTreeSize,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_blobs(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_blobs_selfupdate(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_response_blobs(
	const GsFrameType &FrameType, uint32_t PairedVecLen,
	uint8_t *SizeBufferBlobData, uint32_t SizeBufferBlobSize,
	uint8_t *ObjectBufferBlobData, uint32_t ObjectBufferBlobSize,
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_request_latest_selfupdate_blob(
	gs_bysize_cb_t cb, void *ctx);
int aux_frame_full_write_response_latest_selfupdate_blob(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _GITTEST_FRAME_H_ */
