#include <cstring>

#include <thread>
#include <chrono>
#include <string>
#include <random>

#include <windows.h>
#include <wingdi.h>

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/gui.h>

#include <imgpbempty_384_32_.h>
#include <imgpbfull_384_32_.h>
#include <imgpbblip_96_32_.h>

#define GS_GUI_WIN_FRAMERATE 30

#define GS_GUI_WIN_READIMAGE_B_HEX(hdc, lump, p_img) gs_gui_win_readimage_b_hex(hdc, # lump, lump, sizeof (lump), p_img)

struct AuxImgB
{
	std::string mName;
	int mWidth;
	int mHeight;
	HBITMAP mBitmap;
};

const char GsGuiWinClassName[] = "GsGuiWinClass";
const char GsGuiWinWindowName[] = "Selfupdate";

int gs_gui_win_bitmap_from_rgb(
	HDC hDc,
	int Width, int Height,
	const char *ImgDataBuf, size_t LenImgData,
	HBITMAP *oHBitmap)
{
	/* https://msdn.microsoft.com/en-us/library/ms969901.aspx
	     GetDIBits section example
		 if everything else fails - just call StretchDIBits from WM_PAINT
	*/
	int r = 0;

	HBITMAP hBitmap = NULL;
	BITMAPINFO BitmapInfo = {};

	char *TmpBuf = NULL;

	if (LenImgData != Width * Height * 3)
		GS_ERR_CLEAN(1);

	if (!(TmpBuf = (char *) malloc(Width * Height * 4)))
		GS_ERR_CLEAN(1);

	for (size_t y = 0; y < Height; y++)
		for (size_t x = 0; x < Width; x++) {
			TmpBuf[Width * 4 * y + 4 * x + 0] = ImgDataBuf[Width * 3 * y + 3 * x + 0];
			TmpBuf[Width * 4 * y + 4 * x + 1] = ImgDataBuf[Width * 3 * y + 3 * x + 1];
			TmpBuf[Width * 4 * y + 4 * x + 2] = ImgDataBuf[Width * 3 * y + 3 * x + 2];
			TmpBuf[Width * 4 * y + 4 * x + 3] = 0;
		}

	// FIXME: using hDc instead of hDc2 fixes most fields of GetObject
	if (!(hBitmap = CreateCompatibleBitmap(hDc, Width, Height)))
		GS_ERR_CLEAN(1);

	BitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
	BitmapInfo.bmiHeader.biWidth = Width;
	BitmapInfo.bmiHeader.biHeight = Height;
	BitmapInfo.bmiHeader.biPlanes = 1;
	BitmapInfo.bmiHeader.biBitCount = 32;
	BitmapInfo.bmiHeader.biCompression = BI_RGB;
	BitmapInfo.bmiHeader.biSizeImage = 0;
	BitmapInfo.bmiHeader.biXPelsPerMeter = 0;
	BitmapInfo.bmiHeader.biYPelsPerMeter = 0;
	BitmapInfo.bmiHeader.biClrUsed = 0;
	BitmapInfo.bmiHeader.biClrImportant = 0;

	/* referenced example has hDc for SetDIBits and hDc2 for CreateCompatibleBitmap
	   but describes CreateDIBitmap in terms of using the CreateCompatibleBitmap DC for both calls */
	// FIXME: "The scan lines must be aligned on a DWORD except for RLE-compressed bitmaps."
	//   https://msdn.microsoft.com/en-us/library/windows/desktop/dd162973(v=vs.85).aspx
	GS_ASSERT((unsigned long long)TmpBuf % 4 == 0);
	if (! SetDIBits(hDc, hBitmap, 0, Height, TmpBuf, &BitmapInfo, DIB_RGB_COLORS))
		GS_ERR_CLEAN(1);

	if (oHBitmap)
		*oHBitmap = hBitmap;

clean:
	if (TmpBuf)
		free(TmpBuf);

	if (!!r) {
		if (hBitmap)
			DeleteObject(hBitmap);
	}

	return r;
}

int gs_gui_win_readimage_b_hex(
	HDC hDc,
	const std::string &FName,
	const char *HexBuf, size_t LenHex,
	struct AuxImgB *oImgB)
{
	int r = 0;

	struct AuxImgB ImgB = {};

	struct AuxImg Img = {};
	HBITMAP hBitmap = NULL;

	if (!!(r = gs_gui_readimage_hex(FName, std::string(HexBuf, LenHex), &Img)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_win_bitmap_from_rgb(
		hDc,
		Img.mWidth, Img.mHeight,
		Img.mData.data(), Img.mData.size(),
		&hBitmap)))
	{
		GS_GOTO_CLEAN();
	}

	ImgB.mName = Img.mName;
	ImgB.mWidth = Img.mWidth;
	ImgB.mHeight = Img.mHeight;
	ImgB.mBitmap = hBitmap;

	if (oImgB)
		*oImgB = ImgB;

clean:
	if (!!r) {
		if (hBitmap)
			DeleteObject(hBitmap);
	}

	return r;
}

int gs_gui_win_drawimage_mask_b(
	HDC hDc,
	UINT ColorTransparentRGB,
	struct AuxImgB *ImgDraw,
	int SrcX, int SrcY,
	int Width, int Height,
	int DestX, int DestY)
{
	int r = 0;

	HDC hDc2 = NULL;
	HGDIOBJ hObjectOld = NULL;

	if (!(hDc2 = CreateCompatibleDC(hDc)))
		GS_ERR_CLEAN(1);

	hObjectOld = SelectObject(hDc2, ImgDraw->mBitmap);

	/* TransparentBlt fails on zero dimensions */
	if (Width != 0 && Height != 0)
		if (! TransparentBlt(hDc, DestX, DestY, Width, Height, hDc2, SrcX, SrcY, Width, Height, ColorTransparentRGB))
			GS_ERR_CLEAN(1);

clean:
	if (hObjectOld && hDc2)
		SelectObject(hDc2, hObjectOld);
	if (hDc2)
		DeleteDC(hDc2);

	return r;
}

int gs_gui_win_clear_window(
	HWND Hwnd,
	HDC hDc)
{
	int r = 0;

	RECT WindowRect = {};
	RECT ClearRect = {};
	HBRUSH BgBrush = NULL;

	if (! GetWindowRect(Hwnd, &WindowRect))
		GS_ERR_CLEAN(1);

	ClearRect.left = 0;
	ClearRect.top = 0;
	ClearRect.right = WindowRect.right - WindowRect.left;
	ClearRect.bottom = WindowRect.bottom - WindowRect.top;

	if (! (BgBrush = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF))))
		GS_ERR_CLEAN(1);

	if (! FillRect(hDc, &ClearRect, BgBrush))
		GS_ERR_CLEAN(1);

clean:
	if (BgBrush)
		DeleteObject(BgBrush);

	return r;
}

int gs_gui_win_draw_progress_ratio(
	HDC hDc,
	struct AuxImgB *ImgPbEmpty,
	struct AuxImgB *ImgPbFull,
	int DestX, int DestY,
	int RatioA, int RatioB)
{
	int r = 0;

	float Ratio = 0.0f;
	if (RatioB)
		Ratio = (float)RatioA / RatioB;

	if (!!(r = gs_gui_win_drawimage_mask_b(hDc, GS_GUI_COLOR_MASK_RGB, ImgPbEmpty, 0, 0, ImgPbEmpty->mWidth, ImgPbEmpty->mHeight, DestX, DestY)))
		GS_GOTO_CLEAN();
	if (!!(r = gs_gui_win_drawimage_mask_b(hDc, GS_GUI_COLOR_MASK_RGB, ImgPbFull, 0, 0, ImgPbFull->mWidth * Ratio, ImgPbFull->mHeight, DestX, DestY)))
		GS_GOTO_CLEAN();

clean:

	return r;
}

int gs_gui_win_draw_progress_blip(
  HDC hDc,
  struct AuxImgB *ImgPbEmpty,
  struct AuxImgB *ImgPbBlip,
  int DestX, int DestY,
  int BlipCnt)
{
  int r = 0;

  int SrcX = 0, DrawLeft = 0, DrawWidth = 0;

  if (!!(r = gs_gui_progress_blip_calc(BlipCnt, ImgPbEmpty->mWidth, ImgPbBlip->mWidth, &SrcX, &DrawLeft, &DrawWidth)))
	  GS_GOTO_CLEAN();

  if (!!(r = gs_gui_win_drawimage_mask_b(hDc, GS_GUI_COLOR_MASK_RGB, ImgPbBlip, SrcX, 0, DrawWidth, ImgPbBlip->mHeight, DestX + DrawLeft, DestY)))
	  GS_GOTO_CLEAN();
  if (!!(r = gs_gui_win_drawimage_mask_b(hDc, GS_GUI_COLOR_MASK_RGB, ImgPbEmpty, 0, 0, ImgPbEmpty->mWidth, ImgPbEmpty->mHeight, DestX, DestY)))
	  GS_GOTO_CLEAN();

clean:

  return r;
}

LRESULT CALLBACK WndProc(HWND Hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int r = 0;

	switch (Msg)
	{
	case WM_CREATE:
	{
		if (!!r)
			GS_ASSERT(0);
	}
	break;

	case WM_PAINT:
	{
		HDC hDc = NULL; /* BeingPaint HDC 'released' by EndPaint */
		PAINTSTRUCT Ps = {};
		
		if (!(hDc = BeginPaint(Hwnd, &Ps)))
			{ r = 1; goto cleansub2; }

	cleansub2:
		if (hDc)
			EndPaint(Hwnd, &Ps);
		if (!!r)
			GS_ASSERT(0);
	}
	break;

	default:
		return DefWindowProc(Hwnd, Msg, wParam, lParam);
	}
	return 0;
}

int gs_gui_win_threadfunc()
{
	int r = 0;

	static struct GsLogBase *Ll = GS_LOG_GET("progress_hint");

	int FrameDurationMsec = 1000 / GS_GUI_WIN_FRAMERATE;

	struct GsGuiProgress *Progress = NULL;

	HINSTANCE hInstance = NULL;

	WNDCLASSEX Wc = {};
	HWND Hwnd = 0;
	BOOL Ret = 0;
	MSG Msg = {};

	struct AuxImgB ImgPbEmpty = {};
	struct AuxImgB ImgPbFull = {};
	struct AuxImgB ImgPbBlip = {};

	Progress = new GsGuiProgress();
	if (! (Progress->mProgressHintLog = GS_LOG_GET("progress_hint")))
		GS_ERR_CLEAN(1);
	Progress->mMode = 0; /*ratio*/
	Progress->mRatioA = 0; Progress->mRatioB = 0;
	Progress->mBlipValOld = 0; Progress->mBlipVal = 0; Progress->mBlipCnt = -1;

	/* NOTE: beware GetModuleHandle(NULL) caveat when called from DLL (should not apply here though) */
	if (!(hInstance = GetModuleHandle(NULL)))
		GS_ERR_CLEAN(1);

	Wc.cbSize = sizeof(WNDCLASSEX);
	Wc.style = 0;
	Wc.lpfnWndProc = WndProc;
	Wc.cbClsExtra = 0;
	Wc.cbWndExtra = 0;
	Wc.hInstance = hInstance;
	Wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	Wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	Wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	Wc.lpszMenuName = NULL;
	Wc.lpszClassName = GsGuiWinClassName;
	Wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

	if (!RegisterClassEx(&Wc))
		GS_ERR_CLEAN(1);

	if (!(Hwnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		GsGuiWinClassName,
		GsGuiWinWindowName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, 400, 200,
		NULL, NULL, hInstance, NULL)))
	{
		GS_ERR_CLEAN(1);
	}

	ShowWindow(Hwnd, SW_SHOW);

	if (! UpdateWindow(Hwnd))
		GS_ERR_CLEAN(1);

	{
		HDC hDcStartup = NULL;

		if (!(hDcStartup = GetDC(Hwnd)))
			GS_ERR_CLEAN(1);

		if (!!(r = GS_GUI_WIN_READIMAGE_B_HEX(hDcStartup, imgpbempty_384_32_, &ImgPbEmpty)))
			GS_GOTO_CLEAN();
		if (!!(r = GS_GUI_WIN_READIMAGE_B_HEX(hDcStartup, imgpbfull_384_32_, &ImgPbFull)))
			GS_GOTO_CLEAN();
		if (!!(r = GS_GUI_WIN_READIMAGE_B_HEX(hDcStartup, imgpbblip_96_32_, &ImgPbBlip)))
			GS_GOTO_CLEAN();

		if (hDcStartup)
			ReleaseDC(Hwnd, hDcStartup);
	}

	while (true) {
		static int Cnt00 = -1;
		Cnt00++; Cnt00 = Cnt00 % 100;
		static std::default_random_engine RandomEngine;
		//{ log_guard_t Log(Ll); GS_LOG(I, PF, "RATIO %d OF %d", Cnt00, 100); }
		{ log_guard_t Log(Ll); GS_LOG(I, PF, "BLIP %d", (int)RandomEngine()); }
		auto TimePointStart = std::chrono::system_clock::now();
		while (PeekMessage(&Msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&Msg);
			DispatchMessage(&Msg);
		}

		if (!!(r = gs_gui_progress_update(Progress)))
			GS_GOTO_CLEAN();

		{
			HDC hDc = NULL;

			if (!(hDc = GetDC(Hwnd)))
				GS_ERR_CLEAN(1);

			if (!!(r = gs_gui_win_clear_window(Hwnd, hDc)))
				GS_GOTO_CLEAN();

			switch (Progress->mMode)
			{
			case 0:
			{
				if (!!(r = gs_gui_win_draw_progress_ratio(
					hDc,
					&ImgPbEmpty,
					&ImgPbFull,
					0, 32,
					Progress->mRatioA, Progress->mRatioB)))
				{
					GS_GOTO_CLEAN();
				}
			}
			break;

			case 1:
			{
				if (!!(r = gs_gui_win_draw_progress_blip(
					hDc,
					&ImgPbEmpty,
					&ImgPbBlip,
					0, 96,
					Progress->mBlipCnt)))
				{
					GS_GOTO_CLEAN();
				}
			}
			break;

			default:
				GS_ASSERT(0);
			}

			if (hDc)
				ReleaseDC(Hwnd, hDc);
		}

		std::this_thread::sleep_until(TimePointStart + std::chrono::milliseconds(FrameDurationMsec));
	}

clean:

	return r;
}

int gs_gui_run()
{
	int r = 0;

	if (!!(r = gs_gui_win_threadfunc()))
		GS_GOTO_CLEAN();

clean:

	return r;
}
