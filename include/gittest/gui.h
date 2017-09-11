#ifndef _GITTEST_GUI_H_
#define _GITTEST_GUI_H_

#include <string>

#include <gittest/log.h>

#define GS_GUI_FRAMERATE 30

#define GS_GUI_COLOR_MASK_RGB 0x00FF00
#define GS_GUI_COLOR_MASK_BGR 0x00FF00

struct AuxImg
{
	std::string mName;
	int mWidth;
	int mHeight;
	std::string mData;
};

struct GsGuiProgress
{
	struct GsLogBase *mProgressHintLog;
	int mMode; /* 0:ratio 1:blip */
	int mRatioA, mRatioB;
	int mBlipValOld, mBlipVal, mBlipCnt;
};

int gs_gui_readfile(
	const std::string &FName,
	std::string *oData);

int gs_gui_readimage_data(
	const std::string &FName,
	const std::string &Data,
	struct AuxImg *oImg);
int gs_gui_readimage_file(
	const std::string &FName,
	struct AuxImg *oImg);
int gs_gui_readimage_hex(
	const std::string &FName,
	const std::string &HexStr,
	struct AuxImg *oImg);

int gs_gui_progress_update(struct GsGuiProgress *Progress);

int gs_gui_progress_blip_calc(
	int BlipCnt,
	int ImgPbEmptyWidth, int ImgPbBlipWidth,
	int *oSrcX, int *oDrawLeft, int *oDrawWidth);

#endif /* _GITTEST_GUI_H_ */
