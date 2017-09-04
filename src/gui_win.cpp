#include <cstring>

#include <windows.h>

#include <gittest/misc.h>
#include <gittest/log.h>

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

int gs_gui_win_bitmap_draw(
	HDC hDc,
	int x, int y,
	HBITMAP hBitmap)
{
	int r = 0;

	HDC hDc2 = NULL;
	HGDIOBJ hObjectOld = NULL;
	BITMAP Bitmap = {};

	if (!(hDc2 = CreateCompatibleDC(hDc)))
		GS_ERR_CLEAN(1);

	if (! GetObject(hBitmap, sizeof Bitmap, &Bitmap))
		GS_ERR_CLEAN(1);

	hObjectOld = SelectObject(hDc2, hBitmap);

	if (! TransparentBlt(hDc, x, y, Bitmap.bmWidth, Bitmap.bmHeight, hDc2, 0, 0, Bitmap.bmWidth, Bitmap.bmHeight, 0x00FFFF))
		GS_ERR_CLEAN(1);

clean:
	if (hObjectOld && hDc2)
		SelectObject(hDc2, hObjectOld);
	if (hDc2)
		DeleteDC(hDc2);

	return r;
}

LRESULT CALLBACK WndProc(HWND Hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	int r = 0;

	static HBITMAP hBitmap = NULL;

	switch (Msg)
	{
	case WM_CREATE:
	{
		HDC hDc = NULL;
		
		char D[64 * 64 * 3] = {};

		for (size_t y = 0; y < 64; y++)
			for (size_t x = 0; x < 64; x++) {
				if ((x / 8) % 3 == 0 && (y / 8) % 3 == 0) {
					D[64 * 3 * y + 3 * x + 0] = 0x00;
					D[64 * 3 * y + 3 * x + 1] = 0xFF;
					D[64 * 3 * y + 3 * x + 2] = 0xFF;
				}
				else {
					D[64 * 3 * y + 3 * x + 0] = 0x00;
					D[64 * 3 * y + 3 * x + 1] = 0x00;
					D[64 * 3 * y + 3 * x + 2] = 0xFF;
				}
			}

		if (!(hDc = GetDC(Hwnd)))
			{ r = 1; goto cleansub1; }

		if (!!(r = gs_gui_win_bitmap_from_rgb(hDc, 64, 64, D, sizeof D, &hBitmap)))
			goto cleansub1;

	cleansub1:
		if (hDc)
			ReleaseDC(Hwnd, hDc);
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

		if (!!(r = gs_gui_win_bitmap_draw(hDc, 0, 0, hBitmap)))
			goto cleansub2;

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

	HINSTANCE hInstance = NULL;

	WNDCLASSEX Wc = {};
	HWND Hwnd = 0;
	BOOL Ret = 0;
	MSG Msg = {};

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

	while ((Ret = GetMessage(&Msg, NULL, 0, 0)) != 0)
	{
		if (Ret == -1)
			GS_ERR_CLEAN(1);
		TranslateMessage(&Msg);
		DispatchMessage(&Msg);
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
