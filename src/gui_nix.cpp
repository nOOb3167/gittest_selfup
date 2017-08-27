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
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gittest/misc.h>

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
*/

#define GS_GUI_NIX_XLIB_NAME "libX11.so"

struct AuxImg
{
	std::string mName;
	int mWidth;
	int mHeight;
	std::string mData;
};

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
typedef Window    (* d_XRootWindow_t)(Display *display, int screen_number);
typedef unsigned long(* d_XBlackPixel_t)(Display *display, int screen_number);
typedef unsigned long(* d_XWhitePixel_t)(Display *display, int screen_number);
typedef int       (* d_XMapRaised_t)(Display *display, Window w);
typedef int       (* d_XNextEvent_t)(Display *display, XEvent *event_return);
typedef XImage *  (* d_XCreateImage_t)(Display *display, Visual *visual, unsigned int
	depth, int format, int offset, char *data, unsigned int width,
	unsigned int height, int bitmap_pad, int bytes_per_line);
typedef Status    (* d_XMatchVisualInfo_t)(Display *display, int screen, int depth, int
	klass, XVisualInfo *vinfo_return);
typedef Pixmap    (* d_XCreatePixmap_t)(Display *display, Drawable d, unsigned int width,
	unsigned int height, unsigned int depth);
typedef GC        (* d_XCreateGC_t)(Display *display, Drawable d, unsigned long valuemask,
	XGCValues *values);
typedef int       (* d_XFreeGC_t)(Display *display, GC gc);
typedef int       (* d_XPutImage_t)(Display *display, Drawable d, GC gc, XImage *image, int
	src_x, int src_y, int dest_x, int dest_y, unsigned int width,
	unsigned int height);
typedef int       (* d_XCopyArea_t)(Display *display, Drawable src, Drawable dest, GC gc, int
	src_x, int src_y, unsigned int width, unsigned height, int
	dest_x, int dest_y);
typedef int       (* d_XSelectInput_t)(Display *display, Window w, long event_mask);
typedef Atom      (* d_XInternAtom_t)(Display *display, char *atom_name, Bool only_if_exists);
typedef int       (* d_XSync_t)(Display *display, Bool discard);
typedef Status    (* d_XSendEvent_t)(Display *display, Window w, Bool propagate, long
	event_mask, XEvent *event_send);
typedef Status    (* d_XInitThreads_t)(void);
typedef int       (* d_XDestroyImage_t)(XImage *ximage);

void *XlibHandle = NULL;

d_XOpenDisplay_t        d_XOpenDisplay;
d_XCloseDisplay_t       d_XCloseDisplay;
d_XDefaultScreen_t      d_XDefaultScreen;
d_XCreateSimpleWindow_t d_XCreateSimpleWindow;
d_XRootWindow_t         d_XRootWindow;
d_XBlackPixel_t         d_XBlackPixel;
d_XWhitePixel_t         d_XWhitePixel;
d_XMapRaised_t          d_XMapRaised;
d_XNextEvent_t          d_XNextEvent;
d_XCreateImage_t        d_XCreateImage;
d_XDestroyImage_t       d_XDestroyImage;
d_XMatchVisualInfo_t    d_XMatchVisualInfo;
d_XCreatePixmap_t       d_XCreatePixmap;
d_XCreateGC_t           d_XCreateGC;
d_XFreeGC_t             d_XFreeGC;
d_XPutImage_t           d_XPutImage;
d_XCopyArea_t           d_XCopyArea;
d_XSelectInput_t        d_XSelectInput;
d_XInternAtom_t         d_XInternAtom;
d_XSync_t               d_XSync;
d_XSendEvent_t          d_XSendEvent;
d_XInitThreads_t        d_XInitThreads;

static int gs_gui_nix_xxlibinit();

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
	if (!(d_XCreateImage = (d_XCreateImage_t)dlsym(Handle, "XCreateImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XDestroyImage = (d_XDestroyImage_t)dlsym(Handle, "XDestroyImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XMatchVisualInfo = (d_XMatchVisualInfo_t)dlsym(Handle, "XMatchVisualInfo")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreatePixmap = (d_XCreatePixmap_t)dlsym(Handle, "XCreatePixmap")))
		GS_ERR_CLEAN(1);
	if (!(d_XCreateGC = (d_XCreateGC_t)dlsym(Handle, "XCreateGC")))
		GS_ERR_CLEAN(1);
	if (!(d_XFreeGC = (d_XFreeGC_t)dlsym(Handle, "XFreeGC")))
		GS_ERR_CLEAN(1);
	if (!(d_XPutImage = (d_XPutImage_t)dlsym(Handle, "XPutImage")))
		GS_ERR_CLEAN(1);
	if (!(d_XCopyArea = (d_XCopyArea_t)dlsym(Handle, "XCopyArea")))
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

	d_XInitThreads();

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

int gs_gui_nix_pixmap_from_rgb(Display *Disp, Visual *Visual, Window Window,
	int Width, int Height,
	const char *ImgDataBuf, size_t LenImgData,
	Pixmap *oPix)
{
	/* ImgData is RGB - must be converted */
	int r = 0;

	XImage *Img = NULL;
	Pixmap Pix = 0;
	GC Gc = 0;
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
	if (Img)
		d_XDestroyImage(Img);
	if (DataCpy)
		free(DataCpy);

	return r;
}

int gs_gui_nix_readfile(
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

int gs_gui_nix_readimage(
	const std::string &FName,
	struct AuxImg *oImg)
{
	int r = 0;

	struct AuxImg Img = {};

	std::string Data;

	std::vector<std::string> Tokens;

	std::stringstream ss(FName);
	std::string item;

	if (!!gs_gui_nix_readfile(FName, &Data))
		GS_ERR_CLEAN(1);

	while (std::getline(ss, item, '_'))
		Tokens.push_back(item);

	if (Tokens.size() < 3)
		GS_ERR_CLEAN(1);

	Img.mName = Tokens[0];
	Img.mWidth = stoi(Tokens[1], NULL, 10);
	Img.mHeight = stoi(Tokens[2], NULL, 10);
	Img.mData.swap(Data);

	if (Img.mData.size() != Img.mWidth * Img.mHeight * 3)
		GS_ERR_CLEAN(1);

	if (oImg)
		*oImg = Img;

clean:

	return r;
}

void gs_gui_nix_threadfunc222(Window Win)
{
	int r = 0;

	Display *Disp = NULL;

	XEvent Ev = {};

	if (!(Disp = d_XOpenDisplay(NULL)))
		GS_ERR_CLEAN(1);

	Ev.xclient.type = ClientMessage;
	Ev.xclient.window = Win;
	Ev.xclient.message_type = d_XInternAtom(Disp, (char *) "DUMMY", False);
	Ev.xclient.format = 8; /* data.b active */
	memset(Ev.xclient.data.b, 0, sizeof Ev.xclient.data.b);

	while (true) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
		if (!d_XSendEvent(Disp, Win, False, NoEventMask, &Ev))
			GS_ERR_CLEAN(1);
		d_XSync(Disp, False);
	}

clean:
	if (!!r)
		assert(0);
}

int gs_gui_nix_threadfunc()
{
	int r = 0;

	std::shared_ptr<std::thread> Thread;

	Display *Disp = NULL;
	int s = 0;
	Window Win = 0;
	XEvent Evt;

	Visual *Visual = NULL;

	Pixmap Pix = 0;
	XGCValues GcValues = {};
	GC Gc = 0;

	struct AuxImg Img0 = {};

	if (!!(r = gs_gui_nix_readimage("img2_8_8_.data", &Img0)))
		GS_GOTO_CLEAN();

	if (!(Disp = d_XOpenDisplay(NULL)))
		GS_ERR_CLEAN(1);

	s = d_XDefaultScreen(Disp);

	Win = d_XCreateSimpleWindow(Disp, d_XRootWindow(Disp, s), 10, 10, 400, 400, 10,
		d_XBlackPixel(Disp, s), d_XWhitePixel(Disp, s));

	d_XSync(Disp, False);

	Thread = std::shared_ptr<std::thread>(new std::thread(gs_gui_nix_threadfunc222, Win));

	if (!!(r = gs_gui_nix_auxvisual(Disp, s, &Visual)))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_pixmap_from_rgb(
		Disp, Visual, Win,
		Img0.mWidth, Img0.mHeight,
		Img0.mData.data(), Img0.mData.size(),
		&Pix)))
	{
		GS_GOTO_CLEAN();
	}

	Gc = d_XCreateGC(Disp, Pix, 0, &GcValues);

	d_XSelectInput(Disp, Win, ExposureMask);

	d_XMapRaised(Disp, Win);

	while (true) {
		d_XNextEvent(Disp, &Evt);

		if (Evt.type == Expose) {
			printf("Expose\n");
			d_XCopyArea(Disp, Pix, Win, Gc, 0, 0, 32, 32, 0, 0);
		} else if (Evt.type == ClientMessage) {
			printf("ClientMessage\n");
		}
	}

clean:
	if (Disp)
		d_XCloseDisplay(Disp);

	return r;
}

int gs_gui_run()
{
	int r = 0;

	if (!!(r = gs_gui_nix_xlibinit()))
		GS_GOTO_CLEAN();

	if (!!(r = gs_gui_nix_threadfunc()))
		GS_GOTO_CLEAN();

clean:
	gs_gui_nix_xlibdestroy();

	return r;
}
