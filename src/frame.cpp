#ifdef _MSC_VER
#pragma warning(disable : 4267 4102)  // conversion from size_t, unreferenced label
#endif /* _MSC_VER */

#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstddef>

#include <gittest/misc.h>
#include <gittest/cbuf.h>
#include <gittest/frame.h>
#include <gittest/gittest.h>

GsFrameType aux_frametype_make(const char *TypeName, uint32_t TypeNum) {
	GsFrameType FrameType = {};
	size_t LenTypeName = 0;

	if ((LenTypeName = strnlen(TypeName, sizeof(FrameType.mTypeName))) == sizeof(FrameType.mTypeName))
		GS_ASSERT(0);

	memcpy(FrameType.mTypeName, TypeName, LenTypeName);
	FrameType.mTypeNum = TypeNum;

	return FrameType;
}

bool aux_frametype_equals(const GsFrameType &a, const GsFrameType &b) {
	GS_ASSERT(sizeof a.mTypeName == GS_FRAME_HEADER_STR_LEN);
	bool eqstr = memcmp(a.mTypeName, b.mTypeName, GS_FRAME_HEADER_STR_LEN) == 0;
	bool eqnum = a.mTypeNum == b.mTypeNum;
	/* XOR basically */
	if ((eqstr || eqnum) && (!eqstr || !eqnum))
		GS_ASSERT(0);
	return eqstr && eqnum;
}

int aux_frame_enough_space(uint32_t TotalLength, uint32_t Offset, uint32_t WantedSpace) {
	int r = 0;

	if (! ((TotalLength >= Offset) && (TotalLength - Offset) >= WantedSpace))
		GS_ERR_CLEAN(1);

clean:

	return r;
}

int aux_frame_read_buf(
	uint8_t *DataStart, uint32_t DataLength, uint32_t DataOffset, uint32_t *DataOffsetNew,
	uint8_t *BufStart, uint32_t BufLength, uint32_t BufOffset, uint32_t NumToRead)
{
	int r = 0;

	if (!!(r = aux_frame_enough_space(DataLength, DataOffset, NumToRead)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_enough_space(BufLength, BufOffset, NumToRead)))
		GS_GOTO_CLEAN();

	memcpy(BufStart + BufOffset, DataStart + DataOffset, NumToRead);

	if (DataOffsetNew)
		*DataOffsetNew = DataOffset + NumToRead;

clean:

	return r;
}

int aux_frame_write_buf(uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew, uint8_t *Buf, uint32_t BufLen) {
	int r = 0;

	if (!!(r = aux_frame_enough_space(DataLength, Offset, BufLen)))
		GS_GOTO_CLEAN();

	memcpy(DataStart + Offset, Buf, BufLen);

	if (OffsetNew)
		*OffsetNew = Offset + BufLen;

clean:

	return r;
}

int aux_frame_read_size(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t *oSize, uint32_t *oDataLengthLimit)
{
	int r = 0;

	uint32_t Size = 0;

	if (!!(r = aux_frame_enough_space(DataLength, Offset, SizeOfSize)))
		GS_GOTO_CLEAN();

	aux_LE_to_uint32(&Size, (char *)(DataStart + Offset), SizeOfSize);


	if (oSize)
		*oSize = Size;

	if (oDataLengthLimit) {
		// FIXME: not implemented / senseless for read size, specialize into a read limit
		//*oDataLengthLimit = Offset + SizeOfSize + Size;
		GS_ASSERT(0);
		GS_ERR_CLEAN(1);
	}

	if (OffsetNew)
		*OffsetNew = Offset + SizeOfSize;

clean:

	return r;
}

int aux_frame_write_size(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t Size)
{
	int r = 0;

	GS_ASSERT(SizeOfSize == sizeof(uint32_t));

	if (!!(r = aux_frame_enough_space(DataLength, Offset, SizeOfSize)))
		GS_GOTO_CLEAN();

	aux_uint32_to_LE(Size, (char *)(DataStart + Offset), SizeOfSize);

	if (OffsetNew)
		*OffsetNew = Offset + SizeOfSize;

clean:

	return r;
}

int aux_frame_read_size_ensure(uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew, uint32_t MSize) {
	int r = 0;

	uint32_t SizeFound = 0;

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &SizeFound, NULL)))
		GS_GOTO_CLEAN();

	if (SizeFound != MSize)
		GS_ERR_CLEAN(1);

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_read_size_limit(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t SizeOfSize, uint32_t *oDataLengthLimit)
{
	int r = 0;

	uint32_t Size = 0;

	if (!!(r = aux_frame_enough_space(DataLength, Offset, SizeOfSize)))
		GS_GOTO_CLEAN();

	aux_LE_to_uint32(&Size, (char *)(DataStart + Offset), SizeOfSize);

	if (!!(r = aux_frame_enough_space(DataLength, Offset + SizeOfSize, Size)))
		GS_GOTO_CLEAN();

	if (oDataLengthLimit)
		*oDataLengthLimit = Offset + SizeOfSize + Size;

	if (OffsetNew)
		*OffsetNew = Offset + SizeOfSize;

clean:

	return r;
}

int aux_frame_read_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	GsFrameType *oFrameType)
{
	int r = 0;

	GsFrameType FrameType = {};

	if (!!(r = aux_frame_enough_space(DataLength, Offset, GS_FRAME_HEADER_STR_LEN + GS_FRAME_HEADER_NUM_LEN)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_read_buf(
		DataStart, DataLength, Offset, &Offset,
		(uint8_t *)FrameType.mTypeName, GS_FRAME_HEADER_STR_LEN, 0, GS_FRAME_HEADER_STR_LEN)))
	{
		GS_GOTO_CLEAN();
	}

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_HEADER_NUM_LEN, &FrameType.mTypeNum, NULL)))
		GS_GOTO_CLEAN();

	if (oFrameType)
		*oFrameType = FrameType;

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_write_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	GsFrameType *FrameType)
{
	int r = 0;

	if (!!(r = aux_frame_enough_space(DataLength, Offset, GS_FRAME_HEADER_STR_LEN + GS_FRAME_HEADER_NUM_LEN)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_buf(DataStart, DataLength, Offset, &Offset, (uint8_t *)FrameType->mTypeName, GS_FRAME_HEADER_STR_LEN)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_HEADER_NUM_LEN, FrameType->mTypeNum)))
		GS_GOTO_CLEAN();

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_ensure_frametype(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	const GsFrameType &FrameType)
{
	int r = 0;

	GsFrameType FoundFrameType = {};

	if (!!(r = aux_frame_read_frametype(DataStart, DataLength, Offset, &Offset, &FoundFrameType)))
		GS_GOTO_CLEAN();

	if (! aux_frametype_equals(FoundFrameType, FrameType))
		GS_ERR_CLEAN(1);

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_read_oid(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint8_t *oOid, uint32_t OidSize)
{
	int r = 0;

	GS_ASSERT(OidSize == GIT_OID_RAWSZ && GS_PAYLOAD_OID_LEN == GIT_OID_RAWSZ);

	if (!!(r = aux_frame_read_buf(
		DataStart, DataLength, Offset, &Offset,
		oOid, OidSize, 0, OidSize)))
	{
		GS_GOTO_CLEAN();
	}

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_write_oid(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint8_t *Oid, uint32_t OidSize)
{
	int r = 0;

	GS_ASSERT(OidSize == GIT_OID_RAWSZ && GIT_OID_RAWSZ == GS_PAYLOAD_OID_LEN);

	if (!!(r = aux_frame_write_buf(DataStart, DataLength, Offset, &Offset, Oid, OidSize)))
		GS_GOTO_CLEAN();

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_read_oid_vec(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	void *ctx, gs_bypart_cb_t cb)
{
	int r = 0;

	uint32_t OidNum = 0;

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &OidNum, NULL)))
		GS_GOTO_CLEAN();

	for (uint32_t i = 0; i < OidNum; i++) {
		uint8_t OidBuf[GIT_OID_RAWSZ];
		if (!!(r = aux_frame_read_oid(DataStart, DataLength, Offset, &Offset, OidBuf, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();
		if (!!(r = cb(ctx, (char *)OidBuf, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();
	}

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

/* Dead code */
int aux_frame_read_oid_vec_(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	uint32_t OidNum = 0;
	uint32_t OidVecSize = 0;
	uint8_t *OidVecData = NULL;
	GsStrided OidVecStrided = {};

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &OidNum, NULL)))
		GS_GOTO_CLEAN();

	OidVecSize = OidNum * GIT_OID_RAWSZ;

	// FIXME: hmmm, almost unbounded allocation, from a single uint32_t read off the network
	if (!!(r = cb(ctx, OidVecSize, &OidVecData)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_strided_for_struct_member(OidVecData, 0, offsetof(git_oid, id), OidNum, GIT_OID_RAWSZ, sizeof(git_oid), &OidVecStrided)))
		GS_GOTO_CLEAN();

	for (uint32_t i = 0; i < OidNum; i++) {
		if (!!(r = aux_frame_read_oid(DataStart, DataLength, Offset, &Offset, GS_STRIDED_PIDX(OidVecStrided, i), GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();
	}

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_write_oid_vec(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	const GsStrided OidVec)
{
	int r = 0;

	if (!!(r = aux_frame_write_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, OidVec.mEltNum)))
		GS_GOTO_CLEAN();

	GS_ASSERT(OidVec.mEltSize == GIT_OID_RAWSZ && GIT_OID_RAWSZ == GS_PAYLOAD_OID_LEN);

	for (uint32_t i = 0; i < OidVec.mEltNum; i++) {
		if (!!(r = aux_frame_write_oid(DataStart, DataLength, Offset, &Offset, GS_STRIDED_PIDX(OidVec, i), OidVec.mEltSize)))
			GS_GOTO_CLEAN();
	}

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}

int aux_frame_full_aux_write_empty(
	GsFrameType *FrameType,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	uint32_t Offset = 0;
	uint32_t BufferSize = GS_FRAME_HEADER_LEN + GS_FRAME_SIZE_LEN + 0;
	uint8_t * BufferData = NULL;

	if (!!(r = cb(ctx, BufferSize, &BufferData)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_frametype(BufferData, BufferSize, Offset, &Offset, FrameType)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, 0)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int aux_frame_full_aux_write_oid(
	GsFrameType *FrameType, uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	uint32_t Offset = 0;
	uint32_t PayloadSize = OidSize;
	uint32_t BufferSize = GS_FRAME_HEADER_LEN + GS_FRAME_SIZE_LEN + PayloadSize;
	uint8_t *BufferData = NULL;

	GS_ASSERT(OidSize == GIT_OID_RAWSZ && GIT_OID_RAWSZ == GS_PAYLOAD_OID_LEN);

	if (!!(r = cb(ctx, BufferSize, &BufferData)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_frametype(BufferData, BufferSize, Offset, &Offset, FrameType)))
		GS_GOTO_CLEAN();

	GS_ASSERT(OidSize == GIT_OID_RAWSZ && GIT_OID_RAWSZ == GS_PAYLOAD_OID_LEN);

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, PayloadSize)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_oid(BufferData, BufferSize, Offset, &Offset, Oid, OidSize)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int aux_frame_full_aux_write_oid_vec(
	GsFrameType *FrameType, const GsStrided OidVec,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	uint32_t Offset = 0;
	uint32_t PayloadSize = GS_FRAME_SIZE_LEN + OidVec.mEltNum * OidVec.mEltSize;
	uint32_t BufferSize = GS_FRAME_HEADER_LEN + GS_FRAME_SIZE_LEN + PayloadSize;
	uint8_t *BufferData = NULL;

	GS_ASSERT(OidVec.mEltSize == GIT_OID_RAWSZ && GIT_OID_RAWSZ == GS_PAYLOAD_OID_LEN);

	if (!!(r = cb(ctx, BufferSize, &BufferData)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_frametype(BufferData, BufferSize, Offset, &Offset, FrameType)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, PayloadSize)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_oid_vec(BufferData, BufferSize, Offset, &Offset, OidVec)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int aux_frame_full_aux_read_paired_vec_noalloc(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	uint32_t *oPairedVecLen, uint32_t *oOffsetSizeBuffer, uint32_t *oOffsetObjectBuffer)
{
	int r = 0;

	uint32_t PairedVecLen = 0;
	uint32_t OffsetSizeBuffer = 0;
	uint32_t OffsetObjectBuffer = 0;
	uint32_t OffsetEnd = 0;

	uint32_t CumulativeSize = 0;

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &PairedVecLen, NULL)))
		GS_GOTO_CLEAN();

	OffsetSizeBuffer = Offset;

	OffsetObjectBuffer = OffsetSizeBuffer + GS_FRAME_SIZE_LEN * PairedVecLen;

	if (!!(r = aux_frame_enough_space(DataLength, OffsetSizeBuffer, GS_FRAME_SIZE_LEN * PairedVecLen)))
		GS_GOTO_CLEAN();

	for (uint32_t i = 0; i < PairedVecLen; i++) {
		uint32_t FoundSize = 0;
		aux_LE_to_uint32(&FoundSize, (char *)(DataStart + OffsetSizeBuffer + GS_FRAME_SIZE_LEN * i), GS_FRAME_SIZE_LEN);
		CumulativeSize += FoundSize;
	}

	if (!!(r = aux_frame_enough_space(DataLength, OffsetObjectBuffer, CumulativeSize)))
		GS_GOTO_CLEAN();

	OffsetEnd = OffsetObjectBuffer + CumulativeSize;

	if (oPairedVecLen)
		*oPairedVecLen = PairedVecLen;

	if (oOffsetSizeBuffer)
		*oOffsetSizeBuffer = OffsetSizeBuffer;

	if (oOffsetObjectBuffer)
		*oOffsetObjectBuffer = OffsetObjectBuffer;

	if (OffsetNew)
		*OffsetNew = OffsetEnd;

clean:

	return r;
}

int aux_frame_full_aux_write_paired_vec(
	GsFrameType *FrameType, uint32_t PairedVecLen,
	uint8_t *SizeBufferTreeData, uint32_t SizeBufferTreeSize,
	uint8_t *ObjectBufferTreeData, uint32_t ObjectBufferTreeSize,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	uint32_t Offset = 0;
	uint32_t PayloadSize = GS_FRAME_SIZE_LEN + SizeBufferTreeSize + ObjectBufferTreeSize;
	uint32_t BufferSize = GS_FRAME_HEADER_LEN + GS_FRAME_SIZE_LEN + PayloadSize;
	uint8_t *BufferData = NULL;

	GS_ASSERT(GS_PAYLOAD_OID_LEN == GIT_OID_RAWSZ);

	if (!!(r = cb(ctx, BufferSize, &BufferData)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_frametype(BufferData, BufferSize, Offset, &Offset, FrameType)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, PayloadSize)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, PairedVecLen)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_buf(BufferData, BufferSize, Offset, &Offset, SizeBufferTreeData, SizeBufferTreeSize)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_buf(BufferData, BufferSize, Offset, &Offset, ObjectBufferTreeData, ObjectBufferTreeSize)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int aux_frame_full_write_serv_aux_interrupt_requested(
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(SERV_AUX_INTERRUPT_REQUESTED);

	return aux_frame_full_aux_write_empty(&FrameType, cb, ctx);
}

int aux_frame_full_write_request_latest_commit_tree(
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_LATEST_COMMIT_TREE);

	return aux_frame_full_aux_write_empty(&FrameType, cb, ctx);
}

int aux_frame_full_write_response_latest_commit_tree(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(RESPONSE_LATEST_COMMIT_TREE);

	return aux_frame_full_aux_write_oid(&FrameType, Oid, OidSize, cb, ctx);
}

int aux_frame_full_write_request_treelist(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_TREELIST);

	return aux_frame_full_aux_write_oid(&FrameType, Oid, OidSize, cb, ctx);
}

int aux_frame_full_write_response_treelist(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(RESPONSE_TREELIST);

	return aux_frame_full_aux_write_oid_vec(&FrameType, OidVecStrided, cb, ctx);
}

int aux_frame_full_write_request_trees(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_TREES);

	return aux_frame_full_aux_write_oid_vec(&FrameType, OidVecStrided, cb, ctx);
}

int aux_frame_full_write_response_trees(
	uint32_t PairedVecLen,
	uint8_t *SizeBufferTreeData, uint32_t SizeBufferTreeSize,
	uint8_t *ObjectBufferTreeData, uint32_t ObjectBufferTreeSize,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(RESPONSE_TREES);

	return aux_frame_full_aux_write_paired_vec(&FrameType, PairedVecLen,
		SizeBufferTreeData, SizeBufferTreeSize,
		ObjectBufferTreeData, ObjectBufferTreeSize,
		cb, ctx);
}

int aux_frame_full_write_request_blobs(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_BLOBS);

	return aux_frame_full_aux_write_oid_vec(&FrameType, OidVecStrided, cb, ctx);
}

int aux_frame_full_write_request_blobs_selfupdate(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_BLOBS_SELFUPDATE);

	return aux_frame_full_aux_write_oid_vec(&FrameType, OidVecStrided, cb, ctx);
}

int aux_frame_full_write_request_blobs3(
	GsStrided OidVecStrided,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_BLOBS3);

	return aux_frame_full_aux_write_oid_vec(&FrameType, OidVecStrided, cb, ctx);
}

int aux_frame_full_write_response_blobs(
	const GsFrameType &FrameType, uint32_t PairedVecLen,
	uint8_t *SizeBufferBlobData, uint32_t SizeBufferBlobSize,
	uint8_t *ObjectBufferBlobData, uint32_t ObjectBufferBlobSize,
	gs_bysize_cb_t cb, void *ctx)
{
	GsFrameType FrameTypeTmp = FrameType;

	return aux_frame_full_aux_write_paired_vec(&FrameTypeTmp, PairedVecLen,
		SizeBufferBlobData, SizeBufferBlobSize,
		ObjectBufferBlobData, ObjectBufferBlobSize,
		cb, ctx);
}

int aux_frame_full_write_request_latest_selfupdate_blob(
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(REQUEST_LATEST_SELFUPDATE_BLOB);

	return aux_frame_full_aux_write_empty(&FrameType, cb, ctx);
}

int aux_frame_full_write_response_latest_selfupdate_blob(
	uint8_t *Oid, uint32_t OidSize,
	gs_bysize_cb_t cb, void *ctx)
{
	static GsFrameType FrameType = GS_FRAME_TYPE_DECL(RESPONSE_LATEST_SELFUPDATE_BLOB);

	return aux_frame_full_aux_write_oid(&FrameType, Oid, OidSize, cb, ctx);
}

/** a complete frame is not written - further writes necessary ('PayloadSize' more bytes) */
int aux_frame_part_write_for_payload(
	const GsFrameType &FrameType, uint32_t PayloadSize,
	gs_bysize_cb_t cb, void *ctx)
{
	int r = 0;

	GsFrameType Ft = FrameType;

	uint32_t Offset = 0;
	uint32_t BufferSize = GS_FRAME_HEADER_LEN + GS_FRAME_SIZE_LEN;
	uint8_t *BufferData = NULL;

	if (!!(r = cb(ctx, BufferSize, &BufferData)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_frametype(BufferData, BufferSize, Offset, &Offset, &Ft)))
		GS_GOTO_CLEAN();

	if (!!(r = aux_frame_write_size(BufferData, BufferSize, Offset, &Offset, GS_FRAME_SIZE_LEN, PayloadSize)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

/** FIXME: unused code? */
int aux_frame_read_oid_vec_cpp(
	uint8_t *DataStart, uint32_t DataLength, uint32_t Offset, uint32_t *OffsetNew,
	std::vector<git_oid> *oOidVec)
{
	int r = 0;

	std::vector<git_oid> OidVec;
	uint32_t OidNum = 0;

	if (!!(r = aux_frame_read_size(DataStart, DataLength, Offset, &Offset, GS_FRAME_SIZE_LEN, &OidNum, NULL)))
		GS_GOTO_CLEAN();

	// FIXME: hmmm, almost unbounded allocation, from a single uint32_t read off the network
	OidVec.resize(OidNum);
	for (uint32_t i = 0; i < OidNum; i++) {
		if (!!(r = aux_frame_read_oid(DataStart, DataLength, Offset, &Offset, (uint8_t *)OidVec[i].id, GIT_OID_RAWSZ)))
			GS_GOTO_CLEAN();
	}

	if (oOidVec)
		oOidVec->swap(OidVec);

	if (OffsetNew)
		*OffsetNew = Offset;

clean:

	return r;
}
