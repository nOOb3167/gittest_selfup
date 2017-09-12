#include <cstddef>
#include <cstdio>

#include <vector>
#include <string>
#include <fstream>
#include <sstream>

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/config.h>
#include <gittest/gui.h>

int gs_gui_readfile(
	const std::string &FName,
	std::string *oData)
{
	std::stringstream Stream;
	std::ifstream File(FName.c_str(), std::ios::in | std::ios::binary);
	Stream << File.rdbuf();
	if (File.bad())
		return 1;
	if (oData)
		*oData = Stream.str();
	return 0;
}

int gs_gui_readimage_data(
	const std::string &FName,
	const std::string &Data,
	struct AuxImg *oImg)
{
	int r = 0;

	struct AuxImg Img = {};

	std::vector<std::string> Tokens;

	std::stringstream ss(FName);
	std::string item;

	while (std::getline(ss, item, '_'))
		Tokens.push_back(item);

	if (Tokens.size() < 3)
		GS_ERR_CLEAN(1);

	Img.mName = Tokens[0];
	Img.mWidth = stoi(Tokens[1], NULL, 10);
	Img.mHeight = stoi(Tokens[2], NULL, 10);
	Img.mData = Data;

	if (Img.mData.size() != Img.mWidth * Img.mHeight * 3)
		GS_ERR_CLEAN(1);

	if (oImg)
		*oImg = Img;

clean:

	return r;
}

int gs_gui_readimage_file(
	const std::string &FName,
	struct AuxImg *oImg)
{
	int r = 0;

	struct AuxImg Img = {};

	std::string Data;

	if (!!(r = gs_gui_readfile(FName, &Data)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_readimage_data(FName, Data, oImg)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_gui_readimage_hex(
	const std::string &FName,
	const std::string &HexStr,
	struct AuxImg *oImg)
{
	int r = 0;

	struct AuxImg Img = {};

	std::string Data;

	GS_BYPART_DATA_VAR(String, BypartData);
	GS_BYPART_DATA_INIT(String, BypartData, &Data);

	if (!!gs_config_decode_hex_c(HexStr.data(), HexStr.size(), &BypartData, gs_bypart_cb_String))
		GS_ERR_CLEAN(1);

	if (!!(r = gs_gui_readimage_data(FName, Data, oImg)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_gui_progress_update(struct GsGuiProgress *Progress)
{
  int r = 0;

  static struct GsLogBase *Ll = GS_LOG_GET("progress_hint");

  char *DumpBuf = NULL;
  char *TmpBuf = NULL;
  size_t LenDump = 0;

  GsLogCrashHandlerDumpBufData Data = {};

  std::vector<std::string> Msg;

  if (!(DumpBuf = (char *) malloc(GS_ARBITRARY_LOG_DUMP_FILE_LIMIT_BYTES)))
    GS_ERR_CLEAN(1);
  if (!(TmpBuf = (char *) malloc(GS_ARBITRARY_LOG_DUMP_FILE_LIMIT_BYTES)))
    GS_ERR_CLEAN(1);
  LenDump = GS_ARBITRARY_LOG_DUMP_FILE_LIMIT_BYTES;

  Data.Tripwire = GS_TRIPWIRE_LOG_CRASH_HANDLER_DUMP_BUF_DATA;
  Data.Buf = DumpBuf;
  Data.MaxWritePos = GS_ARBITRARY_LOG_DUMP_FILE_LIMIT_BYTES;
  Data.CurrentWritePos = 0;
  Data.IsOverflow = 0;

  if (!!(r = gs_log_dump_lowlevel(Ll, &Data, gs_log_crash_handler_dump_buf_cb)))
    GS_GOTO_CLEAN();

  for (const char *Ptr = DumpBuf, *Ptr2 = DumpBuf, *End = DumpBuf + Data.CurrentWritePos;
       Ptr < End;
       Ptr = (Ptr2 += 1))
  {
    while (Ptr2 < End && *Ptr2 != '\n')
      Ptr2++;
    Msg.push_back(std::string(Ptr, Ptr2));
  }

  {
    size_t Last = Msg.size() - 1;
    if (Msg.empty())
      GS_ERR_NO_CLEAN(0);
    GS_ASSERT(Msg[Last].size() < GS_ARBITRARY_LOG_DUMP_FILE_LIMIT_BYTES - 1 /*zero*/);
    /* [^]]]: [^x] - negated scanset of x (here x is']'), finally match ']' */
    if (1 != sscanf(Msg[Last].c_str(), "[progress_hint] [%*[^]]]: [%[^]]]", TmpBuf))
      GS_ERR_CLEAN(1);
    if (2 == sscanf(TmpBuf, "RATIO %d OF %d", &Progress->mRatioA, &Progress->mRatioB)) {
      Progress->mMode = 0; /*ratio*/
    }
    else if (1 == sscanf(TmpBuf, "BLIP %d", &Progress->mBlipVal)) {
      if (Progress->mBlipValOld != Progress->mBlipVal) {
	Progress->mBlipValOld = Progress->mBlipVal;
	Progress->mBlipCnt++;
      }
      Progress->mMode = 1; /*blip*/
    }
    else {
      GS_ERR_CLEAN(1);
    }
  }

noclean:

clean:
  if (DumpBuf)
    free(DumpBuf);

  return r;
}

/* FIXME: PbLeft and PbRight cutoffs are actually designed to be adjustable */
int gs_gui_progress_blip_calc(
	int BlipCnt,
	int ImgPbEmptyWidth, int ImgPbBlipWidth,
	int *oSrcX, int *oDrawLeft, int *oDrawWidth)
{
	int r = 0;

	const float Ratio = (float)(BlipCnt % 100) / 100;
	const int BlipLeftHalf = ImgPbBlipWidth / 2;
	const int BlipRightHalf = ImgPbBlipWidth - BlipLeftHalf;
	const int PbLeft = 0;                   /*blip cutoff*/
	const int PbRight = ImgPbEmptyWidth;    /*blip cutoff*/
	const int DrawCenter = ImgPbEmptyWidth * Ratio; /*blip center (rel)*/
	int DrawLeft = DrawCenter - BlipLeftHalf;
	int DrawCut = GS_MAX(PbLeft - DrawLeft, 0);
	int SrcX = 0;
	/* imagine wanting to draw blip at x-plane of 10 (DrawLeft) but skip
	   everything until 15 (PbLeft). you'd want to
	     - start drawing at x-plane 15 (DrawLeft)
	     - draw pixels of blip higher than 5 (10->15) (SrcX)
	   left skip will be done setting DrawLeft and SrcX appropriately
	*/
	SrcX = DrawCut;
	DrawLeft += SrcX;
	/* having adjusted DrawLeft and SrcX for left skip, compute right skip
	   note that after the adjustment width of blip essentially changed
	   right skip will be done setting width (DrawWidth) appropriately */
	int DrawRight = DrawCenter + BlipRightHalf;
	int DrawCut2 = GS_MAX(DrawRight - PbRight, 0);
	int WiddRemainingConsideringSrcX = ImgPbBlipWidth - SrcX;
	int DrawWidth = WiddRemainingConsideringSrcX - DrawCut2;

	if (oSrcX)
		*oSrcX = SrcX;
	if (oDrawLeft)
		*oDrawLeft = DrawLeft;
	if (oDrawWidth)
		*oDrawWidth = DrawWidth;

clean:

	return r;
}
