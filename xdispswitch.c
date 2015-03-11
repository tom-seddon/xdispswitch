#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include <unistd.h>

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/* If true, (try to) handle maximized windows. Do this by
 * un-maximizing, recording the window size, moving, then
 * re-maximizing - thus retaing the unmaximized shape.
 *
 * There's a #define for this because I am still suspicious about it.
 */
#define HANDLE_MAXIMIZED (1)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/* Why is this not in the headers?? */
enum {
    _NET_WM_STATE_REMOVE=0,	/*remove/unset property*/
    _NET_WM_STATE_ADD=1,	/*add/set property*/
    _NET_WM_STATE_TOGGLE=2,	/*toggle property*/
};

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/* XFree :( */
#define XFREE(X)				\
do						\
{						\
    if((X))					\
    {						\
	XFree(X);				\
	(X)=NULL;				\
    }						\
}						\
while(0)

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Rect
{
    int x0,y0,x1,y1;
};
typedef struct Rect Rect;

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Rect MakeRect(int x,int y,int w,int h)
{
    Rect r={x,y,x+w,y+h};
    return r;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int GetRectWidth(const Rect *r)
{
    return r->x1-r->x0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int GetRectHeight(const Rect *r)
{
    return r->y1-r->y0;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#define ATOM(X) static Atom X;
#include "xdispswitch_atoms.inl"
#undef ATOM

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

#define min(A,B) ((A)<(B)?(A):(B))
#define max(A,B) ((A)>(B)?(A):(B))

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void *GetWindowProperty(unsigned long *num_items,Display *display,Window window,Atom property)
{
    Atom actual_type;
    int actual_format;
    unsigned long tmp_num_items,bytes_after;
    unsigned char *prop;    
    int result=XGetWindowProperty(display,	   /* display */
				  window,	   /* window */
				  property,	   /* property */
				  0,		   /* long_offset */
				  ~0UL,		   /* long_length */
				  False,	   /* delete */
				  AnyPropertyType, /* req_type */
				  &actual_type,	/* actual_type_return */
				  &actual_format, /* actual_format_return */
				  &tmp_num_items, /* nitems_return */
				  &bytes_after,	/* bytes_after_return */
				  &prop);	/* prop_return */

    if(result==Success)
    {
	/* char *n=XGetAtomName(display,property); */
	/* printf("Property %s, actual format=%d\n",n,actual_format); */
	/* XFree(n); */
    }
    else
    {
	tmp_num_items=0;
	prop=NULL;
    }

    if(num_items)
	*num_items=tmp_num_items;

    return prop;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void DoStrutEdge(int strut_start,int strut_end,
			int xr_min,int xr_max,
			int *dest,int new_value)
{
    if((xr_min>=strut_start&&xr_min<strut_end)||
       (xr_max>=strut_start&&xr_max<strut_end))
    {
	*dest=new_value;
    }
}

/* The _NET_WORKAREA root property is the largest rect across the
 * whole desktop that doesn't impinge on any docked windows; no good
 * for docked windows that only exist on one screen. So this is a bit
 * more careful. */
static void RemoveDockWindowRects(Display *display,
				  const XineramaScreenInfo *xin_screens,
				  Rect *xin_rects,
				  int num_xin_screens)
{
    for(int x11_screen_idx=0;x11_screen_idx<ScreenCount(display);++x11_screen_idx)
    {
	Screen *x11_screen=ScreenOfDisplay(display,x11_screen_idx);
	Window x11_screen_root=RootWindow(display,x11_screen_idx);

	Window root,parent,*children;
	unsigned num_children;
	if(XQueryTree(display,x11_screen_root,&root,&parent,&children,&num_children)==0)
	    continue;//bleargh

	for(unsigned i=0;i<num_children;++i)
	{
	    Window window=children[i];
	    
	    long left,right,top,bottom;
	    long left_start_y,left_end_y;
	    long right_start_y,right_end_y;
	    long top_start_x,top_end_x;
	    long bottom_start_x,bottom_end_x;

	    Bool got_strut=False;
	    long *strut=GetWindowProperty(NULL,display,window,_NET_WM_STRUT_PARTIAL);
	    if(strut)
	    {
		got_strut=True;

		left=strut[0];
		right=strut[1];
		top=strut[2];
		bottom=strut[3];
		left_start_y=strut[4];
		left_end_y=strut[5];
		right_start_y=strut[6];
		right_end_y=strut[7];
		top_start_x=strut[8];
		top_end_x=strut[9];
		bottom_start_x=strut[10];
		bottom_end_x=strut[11];
		
		XFREE(strut);
	    }
	    else
	    {
		strut=GetWindowProperty(NULL,display,window,_NET_WM_STRUT);

		if(strut)
		{
		    got_strut=True;

		    left=strut[0];
		    right=strut[1];
		    top=strut[2];
		    bottom=strut[3];

		    // TODO: WidthOfScreen/HeightOfScreen? Is that
		    // what you're supposed to do?
			
		    left_start_y=right_start_y=0;
		    left_end_y=right_end_y=HeightOfScreen(x11_screen);

		    top_start_x=bottom_start_x=0;
		    top_end_x=bottom_end_x=WidthOfScreen(x11_screen);
			
		    XFREE(strut);
		}
	    }

	    if(got_strut)
	    {
		/* printf("left=%ld\n",left); */
		/* printf("right=%ld\n",right); */
		/* printf("top=%ld\n",top); */
		/* printf("bottom=%ld\n",bottom); */
		/* printf("left_start_y=%ld\n",left_start_y); */
		/* printf("left_end_y=%ld\n",left_end_y); */
		/* printf("right_start_y=%ld\n",right_start_y); */
		/* printf("right_end_y=%ld\n",right_end_y); */
		/* printf("top_start_x=%ld\n",top_start_x); */
		/* printf("top_end_x=%ld\n",top_end_x); */
		/* printf("bottom_start_x=%ld\n",bottom_start_x); */
		/* printf("bottom_end_x=%ld\n",bottom_end_x); */

		/* This system only really seems to make sense for
		 * rectangular desktops. Say you have a window with a
		 * top of 4, that has begin and end Xs making it span
		 * two monitors that have different Y origins... how
		 * would that work? This code here assumes that the
		 * reserved space starts at the edges of the screen
		 * rect, and if that doesn't overlap the Xinerama
		 * monitor's area, you don't see it.
		 *
		 * I'm not really convinced I'm doing this properly,
		 * but it works - or seems to - for the GNOME
		 * stuff. */
		
		for(int j=0;j<num_xin_screens;++j)
		{
		    Rect *xr=&xin_rects[j];

		    DoStrutEdge(top_start_x,top_end_x,
				xr->x0,xr->x1,
				&xr->y0,max(xr->y0,top));
		    DoStrutEdge(bottom_start_x,bottom_end_x,
				xr->x0,xr->x1,
				&xr->y1,min(xr->y1,HeightOfScreen(x11_screen)-bottom));
		    DoStrutEdge(left_start_y,left_end_y,
				xr->y0,xr->y1,
				&xr->x0,max(xr->x0,left));
		    DoStrutEdge(right_start_y,right_end_y,
				xr->y0,xr->y1,
				&xr->x1,min(xr->x1,WidthOfScreen(x11_screen)-right));
		}
	    }
	}

	XFREE(children);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static int GetScreenForWindow(const Rect *wrect,const Rect *xrs,int num_xrs)
{
    int best=0;
    int64_t best_area=0;

    for(int i=0;i<num_xrs;++i)
    {
	Rect ir={
	    max(xrs[i].x0,wrect->x0),
	    max(xrs[i].y0,wrect->y0),
	    min(xrs[i].x1,wrect->x1),
	    min(xrs[i].y1,wrect->y1),
	};

	int irw=GetRectWidth(&ir);
	int irh=GetRectHeight(&ir);

	if(irw<=0||irh<=0)
	    continue;

	int64_t area=(int64_t)irw*irh;
	if(area>best_area)
	{
	    best=i;
	    best_area=area;
	}
    }

    return best;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

/* static Bool IsExtensionSupported(Display *display,Atom name) */
/* { */
/*     Atom actual_type; */
/*     int actual_format; */
/*     unsigned long num_items,bytes_after; */
/*     unsigned char *prop; */
/*     int result=XGetWindowProperty(display, /\* display *\/ */
/* 				  XDefaultRootWindow(display), /\* window *\/ */
/* 				  _NET_SUPPORTED,  /\* property *\/ */
/* 				  0,		   /\* long_offset *\/ */
/* 				  ~0UL,		   /\* long_length *\/ */
/* 				  False,	   /\* delete *\/ */
/* 				  AnyPropertyType, /\* req_type *\/ */
/* 				  &actual_type,	/\* actual_type_return *\/ */
/* 				  &actual_format, /\* actual_format_return *\/ */
/* 				  &num_items,	  /\* nitems_return *\/ */
/* 				  &bytes_after,	/\* bytes_after_return *\/ */
/* 				  &prop);	/\* prop_return *\/ */
/*     if(result!=Success) */
/*     { */
/* 	fprintf(stderr,"FATAL: failed to get _NET_SUPPORTED list.\n"); */
/* 	return False; */
/*     } */

/*     /\* printf("got %lu.\n",num_items); *\/ */

/*     Atom *atoms=(Atom *)prop; */
    
/*     /\* printf("Extensions list:\n"); *\/ */
/*     /\* for(unsigned long i=0;i<num_items;++i) *\/ */
/*     /\* { *\/ */
/*     /\* 	char *name=XGetAtomName(display,atoms[i]); *\/ */
/*     /\* 	printf("    %lu. %s\n",i,name); *\/ */
/*     /\* 	XFree(name); *\/ */
/*     /\* } *\/ */

/*     Bool supported=False; */
/*     for(unsigned long i=0;i<num_items;++i) */
/*     { */
/* 	if(atoms[i]==name) */
/* 	{ */
/* 	    supported=True; */
/* 	    break; */
/* 	} */
/*     } */

/*     if(atoms) */
/*     { */
/* 	XFree(atoms); */
/* 	atoms=NULL; */
/*     } */

/*     return supported; */
/* } */

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Window GetFocusWindow(Display *display)
{
    Window *focus_window=GetWindowProperty(NULL,display,XDefaultRootWindow(display),_NET_ACTIVE_WINDOW);
    if(!focus_window)
	return None;

    Window w=*focus_window;

    XFREE(focus_window);
    
    return w;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void GetFrameExtentsForWindow(Rect *r,Display *display,Window window)
{
    memset(r,0,sizeof *r);
    
    long *fe=GetWindowProperty(NULL,display,window,_NET_FRAME_EXTENTS);

    if(fe)
    {
	r->x0=fe[0];
	r->x1=fe[1];

	r->y0=fe[2];
	r->y1=fe[3];

	XFREE(fe);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Bool GetWindowRectForWindow(Rect *r,Display *display,Window window)
{
    XWindowAttributes attrs;
    if(XGetWindowAttributes(display,window,&attrs)==0)
	return False;

    /* printf("XWindowAttributes: x=%d y=%d w=%d h=%d border_width=%d\n",attrs.x,attrs.y,attrs.width,attrs.height,attrs.border_width); */

    Window child;
    XTranslateCoordinates(display,window,attrs.root,0,0,&attrs.x,&attrs.y,&child);

    /* printf("XWindowAttributes: x=%d y=%d w=%d h=%d border_width=%d\n",attrs.x,attrs.y,attrs.width,attrs.height,attrs.border_width); */

    *r=MakeRect(attrs.x,attrs.y,attrs.width,attrs.height);

    Rect fe;
    GetFrameExtentsForWindow(&fe,display,window);

    r->x0-=fe.x0;
    r->x1+=fe.x1;
    r->y0-=fe.y0;
    r->y1+=fe.y1;

    return True;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Bool IsWindowManagerStateSet(Display *display,Window window,Atom name)
{
    Bool is_set=False;
    
    unsigned long num_ws;
    Atom *ws=GetWindowProperty(&num_ws,display,window,_NET_WM_STATE);
    if(ws)
    {
	for(unsigned long i=0;i<num_ws;++i)
	{
	    if(ws[i]==name)
	    {
		is_set=True;
		break;
	    }
	}

	XFREE(ws);
    }

    return is_set;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

// http://lists.libsdl.org/pipermail/commits-libsdl.org/2011-February/012224.html
static void ChangeWindowMaximizedFlags(Display *display,Window window,long action,Bool h,Bool v)
{
    if(h||v)
    {
	XEvent e;

	memset(&e,0,sizeof e);

	e.xany.type=ClientMessage;
	e.xclient.message_type=_NET_WM_STATE;
	e.xclient.format=32;
	e.xclient.window=window;
	e.xclient.data.l[0]=action;

	// http://standards.freedesktop.org/wm-spec/1.3/ar01s07.html#sourceindication
	e.xclient.data.l[3]=2;	/* "pagers and other Clients that
				 * represent direct user actions" */

	int i=1;

	if(h)
	    e.xclient.data.l[i++]=_NET_WM_STATE_MAXIMIZED_HORZ;

	if(v)
	    e.xclient.data.l[i++]=_NET_WM_STATE_MAXIMIZED_VERT;

	XSendEvent(display,
		   XDefaultRootWindow(display),
		   True,
		   SubstructureNotifyMask|SubstructureRedirectMask,
		   &e);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//static Bool g_verbose=False;
static Bool g_verbose=True;
static const char * g_log_fname=NULL;

static void vf(const char *fmt,...)
{
    va_list v;

    if(g_verbose)
    {
	va_start(v,fmt);
	vprintf(fmt,v);
	va_end(v);
    }

    if(g_log_fname)
    {
	FILE *f=fopen(g_log_fname,"at");

	if(f)
	{
	    va_start(v,fmt);
	    vfprintf(f,fmt,v);
	    va_end(v);

	    fclose(f);
	    f=NULL;
	}
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void PrintRect(const Rect *r)
{
    vf("(%d,%d) + %dx%d",r->x0,r->y0,GetRectWidth(r),GetRectHeight(r));
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Bool InitialiseX(Display **display)
{
    *display=XOpenDisplay(NULL);
    if(!*display)
    {
	fprintf(stderr,"FATAL: failed to open connection to display.\n");
	return False;
    }
    
    return True;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static Bool InitialiseXinerama(Rect **xin_rects,
			       const XineramaScreenInfo **xin_screens,
			       int *num_xin_screens,
			       Display *display)
{
    int xin_event_base,xin_error_base;
    if(!XineramaQueryExtension(display,&xin_event_base,&xin_error_base))
    {
	fprintf(stderr,"FATAL: Xinerama not available for display.\n");
	return False;
    }

    if(!XineramaIsActive(display))
    {
	fprintf(stderr,"FATAL: Xinerama not active on display.\n");
	return False;
    }

    *xin_screens=XineramaQueryScreens(display,num_xin_screens);
    if(*num_xin_screens<=1)
    {
	fprintf(stderr,"FATAL: Xinerama reports %d screens; must have 2+.\n",*num_xin_screens);
	return False;
    }

    *xin_rects=malloc(*num_xin_screens*sizeof **xin_rects);
    for(int i=0;i<*num_xin_screens;++i)
    {
	const XineramaScreenInfo *x=&(*xin_screens)[i];
	(*xin_rects)[i]=MakeRect(x->x_org,x->y_org,x->width,x->height);
    }
    
    RemoveDockWindowRects(display,*xin_screens,*xin_rects,*num_xin_screens);

    vf("%d Xinerama screen(s):\n",*num_xin_screens);
    for(int i=0;i<*num_xin_screens;++i)
    {
	const XineramaScreenInfo *xs=&(*xin_screens)[i];
	const Rect *xr=&(*xin_rects)[i];

	vf("    %d: #%d, (%d,%d) + %dx%d (rect: ",i,xs->screen_number,xs->x_org,xs->y_org,xs->width,xs->height);
	PrintRect(xr);
	vf(")\n");
    }

    return True;
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

struct Options
{
    Bool help;
    const char *log_fname;
    Bool verbose;
};
typedef struct Options Options;

static const Options DEFAULT_OPTIONS={
    .help=False,
    .log_fname=NULL,
    .verbose=False,
};

static Bool DoOptions(Options *options,int argc,char *argv[])
{
    *options=DEFAULT_OPTIONS;

    for(;;)
    {
	char c=getopt(argc,argv,"hl:v");

	if(c=='h')
	    options->help=True;
	else if(c=='l')
	    options->log_fname=optarg;
	else if(c=='v')
	    options->verbose=True;
	else if(c==-1)
	    return True;
	else
	    return False;
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void PrintHelp(const char *argv0)
{
    printf("Syntax: %s -h -l LOGFILE -v\n",argv0);
    printf("-h          display help\n");
    printf("-l LOGFILE  log stuff to LOGFILE\n");
    printf("-v          log stuff to stdout\n");
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int main(int argc,char *argv[])
{
    Display *display=NULL;
    Rect *xin_rects=NULL;
    int result=EXIT_FAILURE;

    /* Command line stuff. */
    {
	Options options;
	Bool good=DoOptions(&options,argc,argv);
	if(!good||options.help)
	{
	    PrintHelp(argv[0]);

	    if(good)
		return EXIT_SUCCESS;
	    else
		return EXIT_FAILURE;
	}

	g_log_fname=options.log_fname;

	if(g_log_fname)
	{
	    /* Write separator to log file only. */
	    vf("----------------------------------------------------------------\n");
	}
	
	g_verbose=options.verbose;
    }

    /* Initialize X stuff. */
    const XineramaScreenInfo *xin_screens;
    int num_xin_screens;
    {
	if(!InitialiseX(&display))
	    goto done;

#define ATOM(X) X=XInternAtom(display,#X,True); //printf("%s=%lu\n",#X,(X));
#include "xdispswitch_atoms.inl"
#undef ATOM

	if(!InitialiseXinerama(&xin_rects,&xin_screens,&num_xin_screens,display))
	    goto done;
    }
    
    /* Get active window. */
    Window focus;
    {
	focus=GetFocusWindow(display);
	if(focus==None)
	{
	    fprintf(stderr,"FATAL: failed to get focus window.\n");
	    goto done;
	}

	vf("Active window ID: 0x%lX\n",focus);

	/* Don't mess with fullscreen windows. */
	if(IsWindowManagerStateSet(display,focus,_NET_WM_STATE_FULLSCREEN))
	{
	    fprintf(stderr,"FATAL: not touching fullscreen window.\n");
	    goto done;
	}
    }

    /* Get original window details. */
    Rect old_rect;
    {
	if(!GetWindowRectForWindow(&old_rect,display,focus))
	{
	    fprintf(stderr,"FATAL: failed to get focus window's old rect.\n");
	    goto done;
	}
    }

    /* Remove maximized state. */
    Bool max_horz,max_vert;
    {
#if HANDLE_MAXIMIZED
	max_horz=IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_HORZ);
	max_vert=IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_VERT);

	vf("Before: Active window _NET_WM_STATE_MAXIMIZED_HORZ: %s\n",max_horz?"True":"False");
	vf("Before: Active window _NET_WM_STATE_MAXIMIZED_VERT: %s\n",max_vert?"True":"False");

	ChangeWindowMaximizedFlags(display,focus,_NET_WM_STATE_REMOVE,max_horz,max_vert);
#else
	/* Just pretend it wasn't maximized. The post-maximize loop
	 * will drop out immediately and the maximize flags won't be
	 * changed later. */
	max_horz=False;
	max_vert=False;
#endif
    }

    /* Spin until any maximization has been removed. */
    if(max_horz||max_vert)
    {
	int num_tries=0;
	while(IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_HORZ)||
	      IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_VERT))
	{
	    ++num_tries;
	}
	
	vf("After: Active window _NET_WM_STATE_MAXIMIZED_HORZ: %s\n",IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_HORZ)?"True":"False");
	vf("After: Active window _NET_WM_STATE_MAXIMIZED_VERT: %s\n",IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_VERT)?"True":"False");
	vf("(num polls: %d)\n",num_tries);
    }

    /* Get unmaximized window details. */
    Rect focus_rect;
    {
	GetWindowRectForWindow(&focus_rect,display,focus);
    }

    /* Get window details. */
    Rect frame_extents;
    {
	GetFrameExtentsForWindow(&frame_extents,display,focus);
	vf("Frame extents: left=%d right=%d top=%d bottom=%d\n",frame_extents.x0,frame_extents.x1,frame_extents.y0,frame_extents.y1);
    }

    /* Decide which screen it's currently on, and store the
     * (proportional) coordinates of the edges. */
    int screen_idx;
    double tx0,tx1,ty0,ty1;
    {
	screen_idx=GetScreenForWindow(&focus_rect,xin_rects,num_xin_screens);
	
	const Rect *xr=&xin_rects[screen_idx];

	double xrw=GetRectWidth(xr);
	double xrh=GetRectHeight(xr);
	
	if(xrw==0.0||xrh==0.0)
	{
	    fprintf(stderr,"FATAL: Window's screen has zero width or height.\n");
	    goto done;
	}

	tx0=(focus_rect.x0-xr->x0)/xrw;
	tx1=(focus_rect.x1-xr->x0)/xrw;

	ty0=(focus_rect.y0-xr->y0)/xrh;
	ty1=(focus_rect.y1-xr->y0)/xrh;
	
	vf("Active window proportional positions: x0=%.3f y0=%.3f x1=%.3f y1=%.3f\n",tx0,ty0,tx1,ty1);
    }

    /* Pick the next screen. */
    {
	vf("Active window old Xinerama screen: %d\n",screen_idx);
	screen_idx=(screen_idx+1)%num_xin_screens;
	vf("Active window new Xinerama screen: %d\n",screen_idx);
    }
    
    /* Generate a new rect for the window. */
    Rect new_rect;
    {
	const Rect *xr=&xin_rects[screen_idx];
	
	int xrw=GetRectWidth(xr);
	int xrh=GetRectHeight(xr);
	
	new_rect.x0=xr->x0+(int)(tx0*xrw);
	new_rect.y0=xr->y0+(int)(ty0*xrh);
	new_rect.x1=xr->x0+(int)(tx1*xrw);
	new_rect.y1=xr->y0+(int)(ty1*xrh);

	new_rect.x1-=frame_extents.x0+frame_extents.x1;
	new_rect.y1-=frame_extents.y0+frame_extents.y1;

	vf("Active window new rect: ");
	PrintRect(&new_rect);
	vf("\n");
    }

    /* Move the window to its new position and restore maximized state. */
    {
	XMoveResizeWindow(display,focus,new_rect.x0,new_rect.y0,GetRectWidth(&new_rect),GetRectHeight(&new_rect));
	ChangeWindowMaximizedFlags(display,focus,_NET_WM_STATE_ADD,max_horz,max_vert);
    }
    
    XFlush(display);

    result=EXIT_SUCCESS;

done:
    free(xin_rects);
    xin_rects=NULL;
    
    if(display)
    {
	XCloseDisplay(display);
	display=NULL;
    }

    return result;
}
