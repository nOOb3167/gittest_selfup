#ifndef _GITTEST_GUI_H_
#define _GITTEST_GUI_H_

#include <string>

#include <gittest/log.h>

#define GS_GUI_FRAMERATE 30

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

int gs_gui_readimage(
	const std::string &FName,
	struct AuxImg *oImg);

int gs_gui_progress_update(struct GsGuiProgress *Progress);

int gs_gui_progress_blip_calc(
	int BlipCnt,
	int ImgPbEmptyWidth, int ImgPbBlipWidth,
	int *oSrcX, int *oDrawLeft, int *oDrawWidth);

#endif /* _GITTEST_GUI_H_ */
