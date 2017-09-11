#include <cassert>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <random>

#include <gittest/misc.h>
#include <gittest/log.h>
#include <gittest/gui.h>

/*
https://en.wikibooks.org/wiki/X_Window_Programming/Xlib#Example
https://stackoverflow.com/questions/346323/xorg-loading-an-image
wtf apparently 24 depth uses a four byte inmemory format
https://www.x.org/archive/X11R7.5/doc/x11proto/proto.pdf
  "The scanline is quantized in multiples of bits as given by bitmap-scanline-unit."
  and bitmap-scanline-unit is 8,16,32 - notice no 24, must be rounded up
    workaround: at least check the bits_per_pixel member of created XImage
  First lead (meh)
    XListPixmapFormats et al
  Second lead
    XInitImage
    but how the heck is it used
https://tronche.com/gui/x/icccm/sec-4.html#s-4.2.8.1
  WM_DELETE_WINDOW
https://groups.google.com/forum/#!topic/comp.windows.x/oLc9JJDwnZM
  about XDestroyImage (calls XFree on data member..)
https://code.woboq.org/qt5/include/X11/X.h.html
https://refspecs.linuxfoundation.org/LSB_3.1.0/LSB-Desktop-generic/LSB-Desktop-generic/libx11-ddefs.html
  X type definitions (ex Pixmap as XID)
*/

// FIXME: randomly chosen but see xres for ability to detect assigned/unassigned XID range https://linux.die.net/man/3/xres
#define GS_XLIB_XID_MAGIC_SENTINEL 0x13D0896E

#define GS_GUI_NIX_XLIB_NAME "libX11.so"

struct AuxImgP
{
	std::string mName;
	int mWidth;
	int mHeight;
	Pixmap mPix;
};

typedef Display * (* d_XOpenDisplay_t)(char *display_name);
typedef int       (* d_XCloseDisplay_t)(Display *display);
typedef int       (* d_XDefaultScreen_t)(Display *display);
typedef Window    (* d_XCreateSimpleWindow_t)(Display *display, Window parent, int x, int y,
	unsigned int width, unsigned int height, unsigned int border_width, unsigned long border, unsigned long background);
typedef int       (* d_XDestroyWindow_t)(Display *display, Window w);
typedef Window    (* d_XRootWindow_t)(Display *display, int screen_number);
typedef int       (* d_XClearWindow_t)(Display *display, Window w);
typedef unsigned long(* d_XBlackPixel_t)(Display *display, int screen_number);
typedef unsigned long(* d_XWhitePixel_t)(Display *display, int screen_number);
typedef int       (* d_XMapRaised_t)(Display *display, Window w);
typedef int       (* d_XNextEvent_t)(Display *display, XEvent *event_return);
typedef Bool      (* d_XCheckIfEvent_t)(Display *display, XEvent *event_return, Bool(*predicate)(), XPointer arg);
typedef XImage *  (* d_XCreateImage_t)(Display *display, Visual *visual, unsigned int depth, int format, int offset,
	char *data, unsigned int width, unsigned int height, int bitmap_pad, int bytes_per_line);
typedef int       (* d_XDestroyImage_t)(XImage *ximage);
typedef Status    (* d_XMatchVisualInfo_t)(Display *display, int screen, int depth, int klass, XVisualInfo *vinfo_return);
typedef Pixmap    (* d_XCreatePixmap_t)(Display *display, Drawable d, unsigned int width, unsigned int height, unsigned int depth);
typedef Pixmap    (* d_XCreatePixmapFromBitmapData_t)(Display *display, Drawable d, char *data,
	unsigned int width, unsigned int height, unsigned long fg, unsigned long bg, unsigned int depth);
typedef int       (* d_XFreePixmap_t)(Display *display, Pixmap pixmap);
typedef GC        (* d_XCreateGC_t)(Display *display, Drawable d, unsigned long valuemask, XGCValues *values);
typedef int       (* d_XFreeGC_t)(Display *display, GC gc);
typedef int       (* d_XPutImage_t)(Display *display, Drawable d, GC gc, XImage *image,
	int src_x, int src_y, int dest_x, int dest_y, unsigned int width, unsigned int height);
typedef int       (* d_XCopyArea_t)(Display *display, Drawable src, Drawable dest, GC gc,
	int src_x, int src_y, unsigned int width, unsigned height, int dest_x, int dest_y);
typedef int       (* d_XSetClipOrigin_t)(Display *display, GC gc, int clip_x_origin, int clip_y_origin);
typedef int       (* d_XSetClipMask_t)(Display *display, GC gc, Pixmap pixmap);
typedef int       (* d_XSelectInput_t)(Display *display, Window w, long event_mask);
typedef Atom      (* d_XInternAtom_t)(Display *display, char *atom_name, Bool only_if_exists);
typedef int       (* d_XSync_t)(Display *display, Bool discard);
typedef Status    (* d_XSendEvent_t)(Display *display, Window w, Bool propagate, long event_mask, XEvent *event_send);
typedef Status    (* d_XInitThreads_t)(void);
typedef Status    (* d_XSetWMProtocols_t)(Display *display, Window w, Atom *protocols, int count);
/* upboat if you have no what the copy pasted declaration below means - thanks stackoverflow */
typedef int       (* (* d_XSetErrorHandler_t)(int (* handler)(Display *, XErrorEvent *)))();
typedef int       (* (* d_XSetIOErrorHandler_t)(int (* handler)(Display *)))();

void *XlibHandle = NULL;

d_XOpenDisplay_t        d_XOpenDisplay;
d_XCloseDisplay_t       d_XCloseDisplay;
d_XDefaultScreen_t      d_XDefaultScreen;
d_XCreateSimpleWindow_t d_XCreateSimpleWindow;
d_XDestroyWindow_t      d_XDestroyWindow;
d_XClearWindow_t        d_XClearWindow;
d_XRootWindow_t         d_XRootWindow;
d_XBlackPixel_t         d_XBlackPixel;
d_XWhitePixel_t         d_XWhitePixel;
d_XMapRaised_t          d_XMapRaised;
d_XNextEvent_t          d_XNextEvent;
d_XCheckIfEvent_t       d_XCheckIfEvent;
d_XCreateImage_t        d_XCreateImage;
d_XDestroyImage_t       d_XDestroyImage;
d_XMatchVisualInfo_t    d_XMatchVisualInfo;
d_XCreatePixmap_t       d_XCreatePixmap;
d_XCreatePixmapFromBitmapData_t d_XCreatePixmapFromBitmapData;
d_XFreePixmap_t         d_XFreePixmap;
d_XCreateGC_t           d_XCreateGC;
d_XFreeGC_t             d_XFreeGC;
d_XPutImage_t           d_XPutImage;
d_XCopyArea_t           d_XCopyArea;
d_XSetClipOrigin_t      d_XSetClipOrigin;
d_XSetClipMask_t        d_XSetClipMask;
d_XSelectInput_t        d_XSelectInput;
d_XInternAtom_t         d_XInternAtom;
d_XSync_t               d_XSync;
d_XSendEvent_t          d_XSendEvent;
d_XInitThreads_t        d_XInitThreads;
d_XSetWMProtocols_t     d_XSetWMProtocols;
d_XSetErrorHandler_t    d_XSetErrorHandler;
d_XSetIOErrorHandler_t  d_XSetIOErrorHandler;

int gs_gui_progress_destroy(struct GsGuiProgress *Progress)
{
  GS_DELETE(&Progress, GsGuiProgress);
  return 0;
}

int gs_gui_nix_xlibinit()
{
	int r = 0;

	void *Handle = NULL;

	if (!(Handle = dlopen(GS_GUI_NIX_XLIB_NAME, RTLD_LAZY)))
		GS_ERR_CLEAN(1);

	if (!(d_XOpenDisplay = (d_XOpenDisplay_t)dlsym(Handle, "XOpenDisplay")))
		GS_ERR_CLEAN(1);
	if (!(d_XCloseDisplay = (d_XCloseDisplay_t)dlsym(Handle, "XCloseDisplay")))
		GS_ERR_CLEAN(1);
	if (!(d_XDefaultScreen = (d_XDefaultScreen_t)dlsym(Handle, "XDefaultScreen")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreateSimpleWindow = (d_XCreateSimpleWindow_t)dlsym(Handle, "XCreateSimpleWindow")))
		GS_ERR_CLEAN(1);
	if (!(d_XDestroyWindow = (d_XDestroyWindow_t)dlsym(Handle, "XDestroyWindow")))
		GS_ERR_CLEAN(1);
	if (!(d_XClearWindow = (d_XClearWindow_t)dlsym(Handle, "XClearWindow")))
		GS_ERR_CLEAN(1);
	if (!(d_XRootWindow = (d_XRootWindow_t)dlsym(Handle, "XRootWindow")))
		GS_ERR_CLEAN(1);
	if (!(d_XBlackPixel = (d_XBlackPixel_t)dlsym(Handle, "XBlackPixel")))
		GS_ERR_CLEAN(1);
	if (!(d_XWhitePixel = (d_XWhitePixel_t)dlsym(Handle, "XWhitePixel")))
		GS_ERR_CLEAN(1);
	if (!(d_XMapRaised = (d_XMapRaised_t)dlsym(Handle, "XMapRaised")))
		GS_ERR_CLEAN(1);
	if (!(d_XNextEvent = (d_XNextEvent_t)dlsym(Handle, "XNextEvent")))
		GS_ERR_CLEAN(1);
	if (!(d_XCheckIfEvent = (d_XCheckIfEvent_t)dlsym(Handle, "XCheckIfEvent")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreateImage = (d_XCreateImage_t)dlsym(Handle, "XCreateImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XDestroyImage = (d_XDestroyImage_t)dlsym(Handle, "XDestroyImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XMatchVisualInfo = (d_XMatchVisualInfo_t)dlsym(Handle, "XMatchVisualInfo")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreatePixmap = (d_XCreatePixmap_t)dlsym(Handle, "XCreatePixmap")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreatePixmapFromBitmapData = (d_XCreatePixmapFromBitmapData_t)dlsym(Handle, "XCreatePixmapFromBitmapData")))
		GS_ERR_CLEAN(1);
	if (!(d_XFreePixmap = (d_XFreePixmap_t)dlsym(Handle, "XFreePixmap")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreateGC = (d_XCreateGC_t)dlsym(Handle, "XCreateGC")))
		GS_ERR_CLEAN(1);
	if (!(d_XFreeGC = (d_XFreeGC_t)dlsym(Handle, "XFreeGC")))
		GS_ERR_CLEAN(1);
	if (!(d_XPutImage = (d_XPutImage_t)dlsym(Handle, "XPutImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XCopyArea = (d_XCopyArea_t)dlsym(Handle, "XCopyArea")))
		GS_ERR_CLEAN(1);
	if (!(d_XSetClipOrigin = (d_XSetClipOrigin_t)dlsym(Handle, "XSetClipOrigin")))
		GS_ERR_CLEAN(1);
	if (!(d_XSetClipMask = (d_XSetClipMask_t)dlsym(Handle, "XSetClipMask")))
		GS_ERR_CLEAN(1);
	if (!(d_XSelectInput = (d_XSelectInput_t)dlsym(Handle, "XSelectInput")))
		GS_ERR_CLEAN(1);
	if (!(d_XInternAtom = (d_XInternAtom_t)dlsym(Handle, "XInternAtom")))
		GS_ERR_CLEAN(1);
	if (!(d_XSync = (d_XSync_t)dlsym(Handle, "XSync")))
		GS_ERR_CLEAN(1);
	if (!(d_XSendEvent = (d_XSendEvent_t)dlsym(Handle, "XSendEvent")))
		GS_ERR_CLEAN(1);
	if (!(d_XInitThreads = (d_XInitThreads_t)dlsym(Handle, "XInitThreads")))
		GS_ERR_CLEAN(1);
	if (!(d_XSetWMProtocols = (d_XSetWMProtocols_t)dlsym(Handle, "XSetWMProtocols")))
		GS_ERR_CLEAN(1);
	if (!(d_XSetErrorHandler = (d_XSetErrorHandler_t)dlsym(Handle, "XSetErrorHandler")))
		GS_ERR_CLEAN(1);
	if (!(d_XSetIOErrorHandler = (d_XSetIOErrorHandler_t)dlsym(Handle, "XSetIOErrorHandler")))
		GS_ERR_CLEAN(1);

	/* XInitThreads is an API of notoriously poor design.
	   If an another thread/library/framework already calls it, we must not do so.
	   It must be called somewhere, however.
	   Preprocessor guard allows for skipping the call if necessary. */
#ifndef GS_XLIB_NO_INIT_THREADS
	d_XInitThreads();
#endif

noclean:
	XlibHandle = Handle;

clean:
	if (!!r) {
		if (Handle)
			dlclose(Handle);
	}

	return r;
}

int gs_gui_nix_xlibdestroy()
{
	if (XlibHandle)
		dlclose(XlibHandle);
	return 0;
}

/* s:screen (XDefaultScreen) */
int gs_gui_nix_auxvisual(Display *Disp, int s, Visual **oVisual)
{
	int r = 0;

	XVisualInfo VInfo = {};

	if (!d_XMatchVisualInfo(Disp, s, 24, TrueColor, &VInfo))
		GS_ERR_CLEAN(1);
	if (!VInfo.visual ||
		VInfo.depth != 24 ||
		VInfo.c_class != TrueColor ||
		!(VInfo.red_mask == 0xFF0000 && VInfo.green_mask == 0xFF00 && VInfo.blue_mask == 0xFF))
	{
		GS_ERR_CLEAN(1);
	}

	*oVisual = VInfo.visual;

clean:

	return r;
}

int gs_gui_nix_pixmap_from_rgb(
	Display *Disp, Visual *Visual, Window Window,
	int Width, int Height,
	const char *ImgDataBuf, size_t LenImgData,
	Pixmap *oPix)
{
	/* ImgData is RGB - must be converted */
	int r = 0;
	
	Pixmap Pix = GS_XLIB_XID_MAGIC_SENTINEL;

	XImage *Img = NULL;
	GC Gc = nullptr;
	XGCValues GcValues = {};

	char *DataCpy = NULL;

	if (LenImgData != Width * Height * 3)
		GS_ERR_CLEAN(1);

	if (!(DataCpy = (char *)malloc(Width * Height * 4)))
		GS_ERR_CLEAN(1);

	for (size_t y = 0; y < Height; y++)
		for (size_t x = 0; x < Width; x++) {
			/* Xlib is B_G_R_PAD */
			DataCpy[y * Width * 4 + x * 4 + 0] = ImgDataBuf[y * Width * 3 + x * 3 + 2];
			DataCpy[y * Width * 4 + x * 4 + 1] = ImgDataBuf[y * Width * 3 + x * 3 + 1];
			DataCpy[y * Width * 4 + x * 4 + 2] = ImgDataBuf[y * Width * 3 + x * 3 + 0];
			DataCpy[y * Width * 4 + x * 4 + 3] = 0x00;
		}

	if (!(Img = d_XCreateImage(Disp, Visual, 24, ZPixmap,
		0, DataCpy, Width, Height, 32 /*bitmap_pad*/, Width * 4 /*bytes_per_line*/)))
	{
		GS_ERR_CLEAN(1);
	}
	DataCpy = NULL; /* ownership transferred to Img->data after XCreateImage (XImageDestroy will call free(2)) */
	if (!Img ||
		Img->width != Width ||
		Img->height != Height ||
		Img->xoffset != 0 ||
		Img->format != ZPixmap ||
		// Img->byte_order != LSBFirst ||
		Img->bitmap_unit != 32 ||
		//Img->bitmap_bit_order != LSBFirst ||
		Img->bitmap_pad != 32 ||
		Img->depth != 24 ||
		Img->bytes_per_line != Width * 4 ||
		Img->bits_per_pixel != 32 ||
		!(Img->red_mask == 0xFF0000 && Img->green_mask == 0xFF00 && Img->blue_mask == 0xFF))
	{
		GS_ERR_CLEAN(1);
	}

	Pix = d_XCreatePixmap(Disp, Window, Width, Height, 24);

	Gc = d_XCreateGC(Disp, Pix, 0, &GcValues);

	d_XPutImage(Disp, Pix, Gc, Img, 0, 0, 0, 0, Width, Height);

	if (oPix)
		*oPix = Pix;

clean:
	if (!!r) {
		if (Pix != GS_XLIB_XID_MAGIC_SENTINEL)
			d_XFreePixmap(Disp, Pix);
	}

	if (Gc != nullptr)
		d_XFreeGC(Disp, Gc);
	if (Img)
		d_XDestroyImage(Img);
	if (DataCpy)
		free(DataCpy);

	return r;
}

int gs_gui_nix_pixmap_mask_color_from_rgb(
	Display *Disp, Window Win,
	int Width, int Height,
	const char *ImgDataBuf, size_t LenImgData,
	unsigned long MaskColorBGR,
	Pixmap *oPix)
{
  int r = 0;

  Pixmap Pix = GS_XLIB_XID_MAGIC_SENTINEL;

  char *DataCpy = NULL;
  
  int BitNum = Width * Height;
  int ByteNum = (BitNum + 7) / 8;
  
  if (LenImgData != Width * Height * 3)
    GS_ERR_CLEAN(1);

  if (!(DataCpy = (char *)malloc(ByteNum)))
    GS_ERR_CLEAN(1);

	for (size_t y = 0; y < Height; y++)
		for (size_t x = 0; x < Width; x++) {
		  char R = ImgDataBuf[y * Width * 3 + x * 3 + 0];
		  char G = ImgDataBuf[y * Width * 3 + x * 3 + 1];
		  char B = ImgDataBuf[y * Width * 3 + x * 3 + 2];
		  unsigned long ImgColorBGR = (B & 0xFF) + ((G & 0xFF) << 8) + ((R & 0xFF) << 16);
		  bool Masked = ImgColorBGR == MaskColorBGR;
		  int Bit = y * Width + x;
		  int Byte = Bit / 8;
		  int ByteBit = Bit % 8;
		  /* want 0 in mask for MaskColor matching pixels, 1 for others */
		  if (Masked)
		    DataCpy[Byte] = DataCpy[Byte] & (~(1 << ByteBit));
		  else
		    DataCpy[Byte] = DataCpy[Byte] | (1 << ByteBit);
		}

	// FIXME: default screen assumed
	Pix = d_XCreatePixmapFromBitmapData(
	  Disp, Win,
	  DataCpy,
	  Width, Height,
	  d_XWhitePixel(Disp, d_XDefaultScreen(Disp)), d_XBlackPixel(Disp, d_XDefaultScreen(Disp)),
	  1);

       if (oPix)
	 *oPix = Pix;

clean:
       if (!!r) {
	 if (Pix != GS_XLIB_XID_MAGIC_SENTINEL)
	   d_XFreePixmap(Disp, Pix);
       }

       if (DataCpy)
	 free(DataCpy);

  return r;
}

int gs_gui_nix_readimage_p(
	Display *Disp, Visual *Visual, Window Window,
	const std::string &FName,
	struct AuxImgP *oImgP)
{
	int r = 0;

	struct AuxImgP ImgP = {};

	struct AuxImg Img = {};
	Pixmap Pix = GS_XLIB_XID_MAGIC_SENTINEL;

	if (!!(r = gs_gui_readimage(FName, &Img)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_pixmap_from_rgb(
		Disp, Visual, Window,
		Img.mWidth, Img.mHeight,
		Img.mData.data(), Img.mData.size(),
		&Pix)))
	{
		GS_GOTO_CLEAN();
	}

	ImgP.mName = Img.mName;
	ImgP.mWidth = Img.mWidth;
	ImgP.mHeight = Img.mHeight;
	ImgP.mPix = Pix;

	if (oImgP)
		*oImgP = ImgP;

clean:
	if (!!r) {
		if (Pix != GS_XLIB_XID_MAGIC_SENTINEL)
			d_XFreePixmap(Disp, Pix);
	}

	return r;
}

int gs_gui_nix_readimage_mask_p(
	Display *Disp, Window Win,
	const std::string &FName,
	unsigned long MaskColorBGR,
	struct AuxImgP *oImgP)
{
	int r = 0;

	struct AuxImgP ImgP = {};

	struct AuxImg Img = {};
	Pixmap Pix = GS_XLIB_XID_MAGIC_SENTINEL;

	if (!!(r = gs_gui_readimage(FName, &Img)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_pixmap_mask_color_from_rgb(
		Disp, Win,
		Img.mWidth, Img.mHeight,
		Img.mData.data(), Img.mData.size(),
		MaskColorBGR,
		&Pix)))
	{
		GS_GOTO_CLEAN();
	}

	ImgP.mName = Img.mName;
	ImgP.mWidth = Img.mWidth;
	ImgP.mHeight = Img.mHeight;
	ImgP.mPix = Pix;

	if (oImgP)
		*oImgP = ImgP;

clean:
	if (!!r) {
		if (Pix != GS_XLIB_XID_MAGIC_SENTINEL)
			d_XFreePixmap(Disp, Pix);
	}

	return r;
}

int gs_gui_nix_drawimage_mask_p(
				Display *Disp, Drawable Dest, GC Gc,
				struct AuxImgP *ImgMask,
				struct AuxImgP *ImgDraw,
				int SrcX, int SrcY,
				int Width, int Height,
				int DestX, int DestY)
{
  int r = 0;

  GS_ASSERT(ImgMask->mWidth == ImgDraw->mWidth && ImgMask->mHeight == ImgDraw->mHeight);
  
  d_XSetClipOrigin(Disp, Gc, DestX - SrcX, DestY - SrcY);
  d_XSetClipMask(Disp, Gc, ImgMask->mPix);

  d_XCopyArea(Disp, ImgDraw->mPix, Dest, Gc, SrcX, SrcY, Width, Height, DestX, DestY);

 clean:
  
  d_XSetClipMask(Disp, Gc, None);

  return r;
}

int gs_gui_nix_draw_progress_ratio(
  Display *Disp, Drawable Dest, GC Gc,
  struct AuxImgP *ImgPbEmptyMask, struct AuxImgP *ImgPbEmpty,
  struct AuxImgP *ImgPbFullMask, struct AuxImgP *ImgPbFull,
  int DestX, int DestY,
  int RatioA, int RatioB)
{
  int r = 0;

  float Ratio = 0.0f;
  if (RatioB)
    Ratio = (float) RatioA / RatioB;
  
  if (!!(r = gs_gui_nix_drawimage_mask_p(Disp, Dest, Gc, ImgPbEmptyMask, ImgPbEmpty, 0, 0, ImgPbEmpty->mWidth, ImgPbEmpty->mHeight, DestX, DestY)))
    GS_GOTO_CLEAN();
  if (!!(r = gs_gui_nix_drawimage_mask_p(Disp, Dest, Gc, ImgPbFullMask, ImgPbFull, 0, 0, ImgPbFull->mWidth * Ratio, ImgPbFull->mHeight, DestX, DestY)))
    GS_GOTO_CLEAN();

 clean:

  return r;
}

int gs_gui_nix_draw_progress_blip(
  Display *Disp, Drawable Dest, GC Gc,
  struct AuxImgP *ImgPbEmptyMask, struct AuxImgP *ImgPbEmpty,
  struct AuxImgP *ImgPbBlipMask, struct AuxImgP *ImgPbBlip,
  int DestX, int DestY,
  int BlipCnt)
{
  int r = 0;

  int SrcX = 0, DrawLeft = 0, DrawWidth = 0;

  if (!!(r = gs_gui_progress_blip_calc(BlipCnt, ImgPbEmpty->mWidth, ImgPbBlip->mWidth, &SrcX, &DrawLeft, &DrawWidth)))
	  GS_GOTO_CLEAN();

  if (!!(r = gs_gui_nix_drawimage_mask_p(Disp, Dest, Gc, ImgPbBlipMask, ImgPbBlip, SrcX, 0, DrawWidth, ImgPbBlip->mHeight, DestX + DrawLeft, DestY)))
	  GS_GOTO_CLEAN();
  if (!!(r = gs_gui_nix_drawimage_mask_p(Disp, Dest, Gc, ImgPbEmptyMask, ImgPbEmpty, 0, 0, ImgPbEmpty->mWidth, ImgPbEmpty->mHeight, DestX, DestY)))
	  GS_GOTO_CLEAN();

 clean:

  return r;
}

Bool gs_gui_nix_threadfunc222_aux_predicate(Display *Disp, XEvent *Evt, XPointer Arg)
{
	return True;
}

int gs_gui_nix_threadfunc()
{
	int r = 0;

	static struct GsLogBase *Ll = GS_LOG_GET("progress_hint");

	int FrameDurationMsec = 1000 / GS_GUI_FRAMERATE;

	struct GsGuiProgress *Progress = NULL;

	Display *Disp = NULL;
	Window Win = GS_XLIB_XID_MAGIC_SENTINEL;

	Atom wmProtocols = {};
	Atom wmDeleteWindow = {};

	Visual *Visual = NULL;
	struct AuxImgP Img0 = {};
	struct AuxImgP ImgPbEmpty = {};
	struct AuxImgP ImgPbEmptyMask = {};
	struct AuxImgP ImgPbFull = {};
	struct AuxImgP ImgPbFullMask = {};
	struct AuxImgP ImgMask0 = {};
	struct AuxImgP ImgPbBlip = {};
	struct AuxImgP ImgPbBlipMask = {};

	XGCValues GcValues = {};
	GC Gc = nullptr;

	Bool HaveEvent = False;
	XEvent Evt = {};

	Progress = new GsGuiProgress();
	if (! (Progress->mProgressHintLog = GS_LOG_GET("progress_hint")))
	  GS_ERR_CLEAN(1);
	Progress->mMode = 0; /*ratio*/
	Progress->mRatioA = 0; Progress->mRatioB = 0;
	Progress->mBlipValOld = 0; Progress->mBlipVal = 0; Progress->mBlipCnt = -1;

	if (!(Disp = d_XOpenDisplay(NULL)))
		GS_ERR_CLEAN(1);

	wmProtocols = d_XInternAtom(Disp, (char *) "WM_PROTOCOLS", False);
	wmDeleteWindow = d_XInternAtom(Disp, (char *) "WM_DELETE_WINDOW", False);
	
	Win = d_XCreateSimpleWindow(Disp, d_XRootWindow(Disp, d_XDefaultScreen(Disp)), 10, 10, 400, 200, 10,
		d_XBlackPixel(Disp, d_XDefaultScreen(Disp)), d_XWhitePixel(Disp, d_XDefaultScreen(Disp)));

	d_XSync(Disp, False);

	if (! d_XSetWMProtocols(Disp, Win, &wmDeleteWindow, 1))
	  GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_auxvisual(Disp, d_XDefaultScreen(Disp), &Visual)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_readimage_p(Disp, Visual, Win, "img2_8_8_.data", &Img0)))
	  GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_readimage_p(Disp, Visual, Win, "imgpbempty_384_32_.data", &ImgPbEmpty)))
	    GS_GOTO_CLEAN();
	if (!!(r = gs_gui_nix_readimage_mask_p(Disp, Win, "imgpbempty_384_32_.data", GS_GUI_COLOR_MASK_BGR, &ImgPbEmptyMask)))
	  GS_GOTO_CLEAN();
	if (!!(r = gs_gui_nix_readimage_p(Disp, Visual, Win, "imgpbfull_384_32_.data", &ImgPbFull)))
	    GS_GOTO_CLEAN();
	if (!!(r = gs_gui_nix_readimage_mask_p(Disp, Win, "imgpbfull_384_32_.data", GS_GUI_COLOR_MASK_BGR, &ImgPbFullMask)))
	  GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_readimage_mask_p(Disp, Win, "imgmask0_384_32_.data", GS_GUI_COLOR_MASK_BGR, &ImgMask0)))
	  GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_readimage_p(Disp, Visual, Win, "imgpbblip_96_32_.data", &ImgPbBlip)))
	  GS_GOTO_CLEAN();
	if (!!(r = gs_gui_nix_readimage_mask_p(Disp, Win, "imgpbblip_96_32_.data", GS_GUI_COLOR_MASK_BGR, &ImgPbBlipMask)))
	  GS_GOTO_CLEAN();

	Gc = d_XCreateGC(Disp, Img0.mPix, 0, &GcValues);

	d_XSelectInput(Disp, Win, ExposureMask | StructureNotifyMask);

	d_XMapRaised(Disp, Win);

	while (true) {
	  //static int Cnt00 = -1;
	  //Cnt00++; Cnt00 = Cnt00 % 100;
	  static std::default_random_engine RandomEngine;
	  //{ log_guard_t Log(Ll); GS_LOG(I, PF, "RATIO %d OF %d", Cnt00, 100); }
	  { log_guard_t Log(Ll); GS_LOG(I, PF, "BLIP %d", (int) RandomEngine()); }
	  auto TimePointStart = std::chrono::system_clock::now();
	  while ((HaveEvent = d_XCheckIfEvent(
	      Disp, &Evt,
	      (Bool (*)()) gs_gui_nix_threadfunc222_aux_predicate, NULL)))
	  {
		switch (Evt.type)
		{
		case Expose:
		{
			printf("Expose\n");
			d_XCopyArea(Disp, Img0.mPix, Win, Gc, 0, 0, 32, 32, 0, 0);
		}
		break;

		case ClientMessage:
		{
			/* https://tronche.com/gui/x/icccm/sec-4.html#s-4.2.8
			   https://tronche.com/gui/x/icccm/sec-4.html#s-4.2.8.1
			     XEvents containing ClientMessage of WM_PROTOCOLS (ex WM_DELETE_WINDOW)
				 by 'type' field is meant 'message_type' of XClientMessageEvent */
		  if (Evt.xclient.message_type == wmProtocols &&
			  Evt.xclient.format == 32 &&
			  Evt.xclient.data.l[0] == wmDeleteWindow)
		  {
		    printf("ClientMessage WM_PROTOCOLS WM_DELETE_WINDOW\n");
		    d_XDestroyWindow(Disp, Win);
		    Win = GS_XLIB_XID_MAGIC_SENTINEL;
		  }
		  else {
			printf("ClientMessage\n");
		  }
		}
		break;

		case DestroyNotify:
		{
		  GS_ERR_NO_CLEAN(0);
		}
		break;

		default:
			break;
		}
          }

	  if (!!(r = gs_gui_progress_update(Progress)))
	    GS_GOTO_CLEAN();

	  d_XClearWindow(Disp, Win);

	  GS_ASSERT(!gs_gui_nix_drawimage_mask_p(Disp, Win, Gc, &ImgMask0, &ImgPbFull, 0, 0, ImgPbFull.mWidth, ImgPbFull.mHeight, 0, 64));
	  
	  switch (Progress->mMode)
	  {
	  case 0:
	  {
	    if (!!(r = gs_gui_nix_draw_progress_ratio(
	      Disp, Win, Gc,
	      &ImgPbEmptyMask, &ImgPbEmpty,
	      &ImgPbFullMask, &ImgPbFull,
	      0, 32,
	      Progress->mRatioA, Progress->mRatioB)))
	    {
	      GS_GOTO_CLEAN();
	    }
	  }
	  break;

	  case 1:
	  {
	    if (!!(r = gs_gui_nix_draw_progress_blip(
	      Disp, Win, Gc,
	      &ImgPbEmptyMask, &ImgPbEmpty,
	      &ImgPbBlipMask, &ImgPbBlip,
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

	  std::this_thread::sleep_until(TimePointStart + std::chrono::milliseconds(FrameDurationMsec));
	}

noclean:

clean :
	if (Gc != nullptr)
		d_XFreeGC(Disp, Gc);

	if (Win != GS_XLIB_XID_MAGIC_SENTINEL)
		d_XDestroyWindow(Disp, Win);

	if (Disp)
		d_XCloseDisplay(Disp);

	GS_DELETE_F(&Progress, gs_gui_progress_destroy);

	return r;
}

int gs_xlib_error_handler(Display *Disp, XErrorEvent *Evt)
{
  printf("err\n");
}

int gs_xlib_ioerror_handler(Display *Disp)
{
  printf("ioerr\n");
}

int gs_gui_run()
{
	int r = 0;

	if (!!(r = gs_gui_nix_xlibinit()))
		GS_GOTO_CLEAN();

	d_XSetErrorHandler(gs_xlib_error_handler);
	d_XSetIOErrorHandler(gs_xlib_ioerror_handler);
	
	if (!!(r = gs_gui_nix_threadfunc()))
		GS_GOTO_CLEAN();

clean:
	gs_gui_nix_xlibdestroy();

	return r;
}
