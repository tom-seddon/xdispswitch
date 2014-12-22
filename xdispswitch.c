#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>

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

    if(result!=Success)
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

/* The _NET_WORKAREA root property is the largest rect across the
 * whole desktop that doesn't impinge on any docked windows; no good
 * for docked windows that only exist on one screen. */
static void RemoveDockWindowRects(Display *display,
				  const XineramaScreenInfo *xin_screens,
				  Rect *xin_rects,
				  int num_xin_screens)
{
    for(int x11_screen_idx=0;x11_screen_idx<ScreenCount(display);++x11_screen_idx)
    {
	Window x11_screen_root=RootWindow(display,x11_screen_idx);

	Window root,parent,*children;
	unsigned num_children;
	if(XQueryTree(display,x11_screen_root,&root,&parent,&children,&num_children)==0)
	    continue;//bleargh

	for(unsigned i=0;i<num_children;++i)
	{
	    Window window=children[i];

	    if(_NET_WM_WINDOW_TYPE==None||_NET_WM_WINDOW_TYPE_DOCK==None)
	    {
		/* This is a bit odd, but there you go. No point holding the
		 * whole thing to ransom over it. */
		continue;
	    }

	    XTextProperty wm_window_type;
	    if(XGetTextProperty(display,window,&wm_window_type,_NET_WM_WINDOW_TYPE)==0)
		continue;

	    Atom type=*(Atom *)wm_window_type.value;
	    if(type!=_NET_WM_WINDOW_TYPE_DOCK)
		continue;
	    
	    XWindowAttributes attrs;
	    if(XGetWindowAttributes(display,window,&attrs)==0)
		continue;//bleargh

	    if(attrs.map_state!=IsViewable)
		continue;

	    for(int j=0;j<num_xin_screens;++j)
	    {
		const XineramaScreenInfo *xs=&xin_screens[j];
		Rect *xr=&xin_rects[j];

		if(attrs.x==xs->x_org&&
		   attrs.y==xs->y_org&&
		   attrs.width==xs->width)
		{
		    /* Docked to the top. */
		    xr->y0=max(xr->y0,attrs.y+attrs.height);
		}
		else if(attrs.y==xs->x_org&&
			attrs.y==xs->y_org+xs->height-attrs.height&&
			attrs.width==xs->width)
		{
		    /* Docked to the bottom. */
		    xr->y1=min(xr->y1,attrs.y);
		}
		else if(attrs.x==xs->x_org&&
			attrs.y==xs->y_org&&
			attrs.height==xs->height)
		{
		    /* Docked to the left. */
		    xr->x0=max(xr->x0,attrs.x+attrs.width);
		}
		else if(attrs.x==xs->x_org+xs->width-attrs.width&&
			attrs.y==xs->y_org&&
			attrs.height==xs->height)
		{
		    /* Docked to the right. */
		    xr->x1=min(xr->x1,attrs.x);
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

    /* printf("GetScreenForWindow: Window rect: %d %d %d %d\n",wrect->x0,wrect->y0,wrect->x1,wrect->y1); */

    for(int i=0;i<num_xrs;++i)
    {
	Rect ir={
	    max(xrs[i].x0,wrect->x0),
	    max(xrs[i].y0,wrect->y0),
	    min(xrs[i].x1,wrect->x1),
	    min(xrs[i].y1,wrect->y1),
	};

	/* printf("    Screen %d rect: %d %d %d %d\n",i,ir.x0,ir.y0,ir.x1,ir.y1); */

	if(ir.x1<=ir.x0||ir.y1<=ir.y0)
	{
	    printf("    (window is out of bounds)\n");
	    continue;
	}

	int64_t area=(int64_t)(ir.x1-ir.x0)*(ir.y1-ir.y0);
	/* printf("    Window area in this screen: %ld\n",area); */
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

    r->x0=attrs.x;
    r->x1=attrs.x+attrs.width;

    r->y0=attrs.y;
    r->y1=attrs.y+attrs.height;

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
		   False,
		   SubstructureNotifyMask|SubstructureRedirectMask,
		   &e);
    }
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//static Bool g_verbose=False;
static Bool g_verbose=True;

static void vf(const char *fmt,...)
{
    if(!g_verbose)
	return;

    va_list v;

    va_start(v,fmt);
    vprintf(fmt,v);
    va_end(v);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

static void PrintRect(const Rect *r)
{
    vf("(%d,%d) + %dx%d",r->x0,r->y0,GetRectWidth(r),GetRectHeight(r));
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

int main(void)
{
    Display *display=NULL;
    Rect *xin_rects=NULL;
    int result=EXIT_FAILURE;
    
    display=XOpenDisplay(NULL);
    if(!display)
    {
	fprintf(stderr,"FATAL: failed to open connection to display.\n");
	goto done;
    }

#define ATOM(X) X=XInternAtom(display,#X,True); //printf("%s=%lu\n",#X,(X));
#include "xdispswitch_atoms.inl"
#undef ATOM

    int xin_event_base,xin_error_base;
    if(!XineramaQueryExtension(display,&xin_event_base,&xin_error_base))
    {
	fprintf(stderr,"FATAL: Xinerama not available for display.\n");
	goto done;
    }

    if(!XineramaIsActive(display))
    {
	fprintf(stderr,"FATAL: Xinerama not active on display.\n");
	goto done;
    }

    int num_xin_screens;
    const XineramaScreenInfo *xin_screens=XineramaQueryScreens(display,&num_xin_screens);
    if(num_xin_screens<=1)
    {
	fprintf(stderr,"FATAL: Xinerama reports %d screens; must have 2+.\n",num_xin_screens);
	goto done;
    }

    xin_rects=malloc(num_xin_screens*sizeof(Rect));
    for(int i=0;i<num_xin_screens;++i)
    {
	const XineramaScreenInfo *x=&xin_screens[i];
	xin_rects[i]=MakeRect(x->x_org,x->y_org,x->width,x->height);
    }
    
    RemoveDockWindowRects(display,xin_screens,xin_rects,num_xin_screens);

    vf("%d Xinerama screen(s):\n",num_xin_screens);
    for(int i=0;i<num_xin_screens;++i)
    {
	const XineramaScreenInfo *xs=&xin_screens[i];
	const Rect *xr=&xin_rects[i];

	vf("    %d: #%d, (%d,%d) + %dx%d (rect: ",i,xs->screen_number,xs->x_org,xs->y_org,xs->width,xs->height);
	PrintRect(xr);
	vf(")\n");
    }

    Window focus=GetFocusWindow(display);
    if(focus==None)
    {
	fprintf(stderr,"FATAL: failed to get focus window.\n");
	goto done;
    }

    vf("Active window ID: 0x%lX\n",focus);

    if(IsWindowManagerStateSet(display,focus,_NET_WM_STATE_FULLSCREEN))
    {
	fprintf(stderr,"FATAL: not touching fullscreen window.\n");
	goto done;
    }

    /* Remove maximized state. */
    Bool max_horz=IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_HORZ);
    Bool max_vert=IsWindowManagerStateSet(display,focus,_NET_WM_STATE_MAXIMIZED_VERT);

    vf("Active window _NET_WM_STATE_MAXIMIZED_HORZ: %s\n",max_horz?"True":"False");
    vf("Active window _NET_WM_STATE_MAXIMIZED_VERT: %s\n",max_vert?"True":"False");

    ChangeWindowMaximizedFlags(display,focus,_NET_WM_STATE_REMOVE,max_horz,max_vert);

    /* Get window details. */
    Rect focus_rect;
    if(!GetWindowRectForWindow(&focus_rect,display,focus))
    {
	fprintf(stderr,"FATAL: failed to get focus window's rect.\n");
	goto done;
    }

    vf("Active window old rect: ");
    PrintRect(&focus_rect);
    vf("\n");

    Rect frame_extents;
    GetFrameExtentsForWindow(&frame_extents,display,focus);
    vf("Frame extents: left=%d right=%d top=%d bottom=%d\n",frame_extents.x0,frame_extents.x1,frame_extents.y0,frame_extents.y1);

    /* Decide which screen it's currently on, and store the
     * (proportional) coordinates of the edges. */
    int screen_idx=GetScreenForWindow(&focus_rect,xin_rects,num_xin_screens);

    double tx0,tx1,ty0,ty1;
    {
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
    }

    vf("Active window proportional positions: x0=%.3f y0=%.3f x1=%.3f y1=%.3f\n",tx0,ty0,tx1,ty1);

    /* Pick the next screen. */
    vf("Active window old Xinerama screen: %d\n",screen_idx);
    screen_idx=(screen_idx+1)%num_xin_screens;
    vf("Active window new Xinerama screen: %d\n",screen_idx);

    /* Generate a new rect for the window. */
    Rect new_rect;
    const Rect *xr=&xin_rects[screen_idx];
    {
	int xrw=GetRectWidth(xr);
	int xrh=GetRectHeight(xr);
	
	new_rect.x0=xr->x0+(int)(tx0*xrw);
	new_rect.y0=xr->y0+(int)(ty0*xrh);
	new_rect.x1=xr->x0+(int)(tx1*xrw);
	new_rect.y1=xr->y0+(int)(ty1*xrh);

	new_rect.x1-=frame_extents.x0+frame_extents.x1;
	new_rect.y1-=frame_extents.y0+frame_extents.y1;
    }

    vf("Active window new rect: ");
    PrintRect(&new_rect);
    vf("\n");

    /* printf("New rect:\n    Screen: "); */
    /* PrintRect(xr); */
    /* printf("\n    Window: "); */
    /* PrintRect(&new_rect); */
    /* printf("\n"); */
    /* printf("    Proportional: x=%f %f y=%f %f\n",tx0,tx1,ty0,ty1); */

    /* Move the window to its new position. */
    XMoveResizeWindow(display,focus,new_rect.x0,new_rect.y0,GetRectWidth(&new_rect),GetRectHeight(&new_rect));

    /* Restore maximized state. */
    ChangeWindowMaximizedFlags(display,focus,_NET_WM_STATE_ADD,max_horz,max_vert);
    
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
