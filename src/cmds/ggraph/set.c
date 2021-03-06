#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <math.h>
#include "ggraph.h"
#include "ggraphdefs.h"

/* set any unset defaults for lines */
setdefault () {
    register int    i;
    for (curline = 0; ((curline != cg.maxlines) && (cl != NULL)); ++curline) {
	if (!cl->llabsw)
	    cl->llabsw = FALSE;
	if (cl->mtype == -1)
	    cl->mtype = CROSS;
	if (cl->ltype == -1)
	    cl->ltype = BRUSH_NORMAL;
	if (cl->ctype == -1)
	    cl->ctype = LINE;
	if(cl->llabel.t_size == -1)
	    cl->llabel.t_size = 2;
	if(cl->llabel.t_font == -1)
	    cl->llabel.t_font = ROMAN;
	if(cl->llelabel.t_size == -1)
	    cl->llelabel.t_size = 2;
	if(cl->llelabel.t_font == -1)
	    cl->llelabel.t_font = ROMAN;
    }
    if (cg.gtype == -1)
	cg.gtype = LINEAR;
    if (cg.xpreci1 == -1)
	cg.xpreci1 = 4;
    if (cg.ypreci1 == -1)
	cg.ypreci1 = 4;
    if (cg.symbolsz == -1)
	cg.symbolsz = 3.0;
    if (cg.logxsw == -1)
	cg.logxsw = 0;
    if (cg.logxtick == -1)
	cg.logxtick = 10.0;
    if (cg.logysw == -1)
	cg.logysw = 0;
    if (cg.logytick == -1)
	cg.logytick = 10.0;
    if (cg.gframe.fsize == -1)
	cg.gframe.fsize = BRUSH_THIN;
    if (cg.lframe.fsize == -1)
	cg.lframe.fsize = BRUSH_THIN;
    if (cg.lframe.fsize == -1)
	cg.lframe.fsize = BRUSH_THIN;
    if (cg.gtitle.t_size == -1)
	cg.gtitle.t_size = 3;
    if (cg.xtick.t_size == -1)
	cg.xtick.t_size = 2;
    if (cg.ytick.t_size == -1)
	cg.ytick.t_size = 2;
    if (cg.xlabel.t_size == -1)
	cg.xlabel.t_size = 2;
    if (cg.ylabel.t_size == -1)
	cg.ylabel.t_size = 2;
    if (cg.legend.t_size == -1)
	cg.legend.t_size = 2;
    if (cg.yvert == -1)
	cg.yvert = 1;
    if (cg.gtitle.t_font == -1)
	cg.gtitle.t_font = ROMAN;
    if (cg.legend.t_font == -1)
	cg.legend.t_font = ROMAN;
    if (cg.xlabel.t_font == -1)
	cg.xlabel.t_font = ROMAN;
    if (cg.ylabel.t_font == -1)
	cg.ylabel.t_font = ROMAN;
    if (cg.xtick.t_font == -1)
	cg.xtick.t_font = ROMAN;
    if (cg.ytick.t_font == -1)
	cg.ytick.t_font = ROMAN;
    if (cg.xgrid_type == -1)
	cg.xgrid_type = BRUSH_DOT;
    if (cg.ygrid_type == -1)
	cg.ygrid_type = BRUSH_DOT;
    if (cg.firstxsw == -1)
	cg.firstxsw = 0;
    if (cg.firstysw == -1)
	cg.firstysw = 0;
    if(legendf){
      calc_legend(&cg.xplotmax,&cg.yorigin);	/* figure out lengend stuff */
      cg.yplotmax = YPLOTMAX;
    }else{
      cg.xplotmax = XPLOTMAX;
      cg.yplotmax = YPLOTMAX;
    }
    cg.gframe.frame1x = FRAME1X;	/* set frame boundaries */
    cg.gframe.frame2x = FRAME2X;
    cg.gframe.frame3x = FRAME3X;
    cg.gframe.frame4x = FRAME4X;
    cg.gframe.frame1y = FRAME1Y;
    cg.gframe.frame2y = FRAME2Y;
    cg.gframe.frame3y = FRAME3Y;
    cg.gframe.frame4y = FRAME4Y;
}
/* set the axis limits and centers */
setlim () {
    float   xrange,
            yrange;		/* range along each axis */
    float   xtestr,
            ytestr;		/* range along each axis to test */
    register int    n,
                    factor;
    int     axmax;
    double  pow ();

    n = 0;
    if ((cg.dtickx == 0) || (cg.logxsw == 1)) {
	xrange = cg.gmaxx - cg.gminx;
	if (xrange < 1.0)
	    factor = -1;
	else
	    factor = 1;
	if (factor == 1) {
	    while (TRUE) {
		xtestr = pow ((double) 10.0, (double) n);
		if ((xrange / xtestr) < 1.0)
		    break;
		n = n + factor;
	    }
	}
	else {
	    while (TRUE) {
		xtestr = pow ((double) 10.0, (double) n);
		if ((xrange / xtestr) > 1.0) {
		    factor = 0;
		    break;
		}
		n = n + factor;
	    }
	    if (cg.xpreci2 == -1)
		cg.xpreci2 = -n + 1;
	}
	cg.dtickx = pow ((double) 10.0, (double) (n - factor));
	axmax = xrange / cg.dtickx;
	if (cg.logxsw) {
if(debug)printf("xmaxtic %d %f\n", cg.maxtickx, cg.stickx);
	    if (cg.maxtickx == 0)
		cg.maxtickx = (int) ((log10 ((double) cg.gmaxx) /
			    log10 ((double) cg.logxtick)));
	    if (cg.stickx != 0)
		cg.mintickx = cg.xoffset = (int)(log10((double)cg.stickx) /
			log10 ((double) (cg.logxtick)));
	    else {
		cg.mintickx = cg.xoffset = (int)(log10((double)cg.gminx) /
			log10 ((double) (cg.logxtick)));
	    }
	    if (debug){
		printf ("decide1 %f %f\n", cg.gminx, (cg.gmaxx -
			    pow ((double) cg.logxtick, (double) cg.maxtickx)));
		printf ("decide2 %f %f\n", cg.gmaxx, (cg.gminx -
			    pow ((double) cg.logxtick, (double) cg.mintickx)));
		printf ("decide3 %f %f\n", cg.gmaxx, (cg.gminx -
			    (float)pow ((double) cg.logxtick, (double) cg.mintickx)));
		printf ("decide4 %f %f\n", cg.gmaxx, ((double)cg.gminx -
			    pow ((double) cg.logxtick, (double) cg.mintickx)));
	    }
	    if ((cg.gmaxx - pow ((double) cg.logxtick, (double) cg.maxtickx))
		    > 0.0)
		cg.maxtickx++;
	    if (cg.stickx == 0.0)
		if ((cg.gminx - (float) pow ((double) cg.logxtick, (double) cg.mintickx))
			< 0.0) {
		    cg.mintickx--;
		    cg.xoffset--;
		}
	    if (!cg.numtickx)
		cg.numtickx = abs ((int) cg.maxtickx - (int) cg.mintickx);
	    cg.xoffset = (int) cg.xoffset;
	    if (debug)
		printf ("maxt %d mint %d numt %d off %f\n",
		  cg.maxtickx, cg.mintickx, cg.numtickx, cg.xoffset);
	}else{
	    if (!cg.numtickx)
	      if((axmax * cg.dtickx) < xrange)
		cg.numtickx = axmax + 1;
	      else
		cg.numtickx = axmax;
	      if (!cg.stickx)
		cg.stickx = cg.gminx;
	      cg.xoffset = cg.gminx;
	}
    }else
      cg.xoffset = cg.stickx;
    n = 0;
    if ((cg.dticky == 0) || (cg.logysw == 1)) {
	yrange = cg.gmaxy - cg.gminy;
	if (yrange < 1.0)
	    factor = -1;
	else
	    factor = 1;
	if (factor == 1) {
	    while (TRUE) {
		ytestr = pow ((double) 10.0, (double) n);
		if ((yrange / ytestr) < 1.0)
		    break;
		n = n + factor;
	    }
	}
	else {
	    while (TRUE) {
		ytestr = pow ((double) 10.0, (double) n);
		if ((yrange / ytestr) > 1.0) {
		    factor = 0;
		    break;
		}
		n = n + factor;
	    }
	    if (cg.ypreci2 == -1)
		cg.ypreci2 = -n + 1;
	}
	cg.dticky = pow ((double) 10.0, (double) (n - factor));
	axmax = yrange / cg.dticky;
	if (cg.logysw) {
	    if (cg.maxticky == 0)
		cg.maxticky = (int) ((log10 ((double) cg.gmaxy) /
			    log10 ((double) cg.logytick)));
	    if (cg.sticky != 0)
		cg.minticky = cg.yoffset = (int) ((log10 ((double) cg.sticky) /
			    log10 ((double) (cg.logytick))));
	    else {
		cg.minticky = cg.yoffset = (int) ((log10 ((double) cg.gminy) /
			    log10 ((double) (cg.logytick))));
	    }
	    if ((cg.gmaxy - pow ((double) cg.logytick, (double) cg.maxticky))
		    > 0.0)
		cg.maxticky++;
	    if (debug)
		printf ("decide %f %f\n", cg.gminy, (cg.gminy -
			    pow ((double) cg.logytick, (double) cg.minticky)));
	    if (cg.sticky == 0.0)
		if ((cg.gminy - (float) pow ((double) cg.logytick, (double) cg.minticky))
			< 0.0) {
		    cg.minticky--;
		    cg.yoffset--;
		}
	    if (!cg.numticky)
		cg.numticky = abs ((int) cg.maxticky - (int) cg.minticky);
	    cg.yoffset = (int) cg.yoffset;
	    if (debug)
		printf ("maxt %d mint %d numt %d\n", cg.maxticky, cg.minticky,
			cg.numticky);
	}else{
	    if (!cg.numticky)
	      if((axmax * cg.dticky) < yrange)
		cg.numticky = axmax + 1;
	      else
		cg.numticky = axmax;
	      if (!cg.sticky)
		cg.sticky = cg.gminy;
	      cg.yoffset = cg.gminy;
	}
    }else
      cg.yoffset = cg.sticky;
    if ((cg.numtickx) < 3 && !cg.logxsw) {
	cg.numtickx = cg.numtickx * 2;
	cg.dtickx = cg.dtickx / 2.0;
	if((cg.dtickx < 1.0)&&(cg.xpreci2 == -1)) {
	   cg.xpreci1 = 1;
	   cg.xpreci2 = 1;
	}
    }
    if ((cg.numticky < 3) && !cg.logysw) {
	cg.numticky = cg.numticky * 2;
	cg.dticky = cg.dticky / 2.0;
	if((cg.dticky < 1.0)&&(cg.ypreci2 == -1)){
	   cg.ypreci1 = 1;
	   cg.ypreci2 = 1;
	}
    }
/* to calculate scale -> PLOTMAX - origin > scale*maxtick*dtick */
/* therefore scale = (PLOTMAX - origin)/(maxtick*dtick) */
 /* center is in user units */
    cg.xcenter = (cg.numtickx * cg.dtickx) / 2.0;
    cg.ycenter = (cg.numticky * cg.dticky) / 2.0;
    calc_text(&cg.xorigin, &cg.yorigin);
    if(debug)printf("origins from text %f %f\n", cg.xorigin, cg.yorigin);
    if(debug)printf("X Label %f %f\n", cg.xlabel.t_xpos, cg.xlabel.t_ypos);
    if(debug)printf("Y Label %f %f\n", cg.ylabel.t_xpos, cg.ylabel.t_ypos);
 /* set scale factors */
    cg.scalex = (cg.xplotmax - cg.xorigin) / (cg.numtickx * cg.dtickx);
    cg.scaley = (cg.yplotmax - cg.yorigin) / (cg.numticky * cg.dticky);
    if (debug)
	fprintf (stderr, "dtickx %f maxtick %d stickx %f\n\
 numtick %d scale %f center %f offset %f\n",
		cg.dtickx, cg.maxtickx, cg.stickx, cg.numtickx, 
		cg.scalex, cg.xcenter, cg.xoffset);
    if (debug)
	fprintf (stderr, "dticky %f maxtick %d sticky %f\n\
 numtick %d scale %f center %f offset %f\n",
		cg.dticky, cg.maxticky, cg.sticky, cg.numticky,
		cg.scaley, cg.ycenter, cg.yoffset);

 /* title is in device units since it's */
 /* only used once might as well do it here */
 /*    cg.gtitle.t_xpos = (cg.xcenter * cg.scalex) + cg.xorigin - 
	((cg.xorigin - cg.gframe.frame1x)/2);*/
    if(!cg.gtitle.t_xpos)
        cg.gtitle.t_xpos = ((cg.gframe.frame3x - cg.gframe.frame1x)/2) + 
	cg.gframe.frame1x;
    if(!cg.gtitle.t_ypos)
        cg.gtitle.t_ypos = (cg.numticky * cg.dticky * cg.scaley)
	 + cg.yorigin + 25;
    return;
}

calc_text(xo, yo)
float *xo, *yo;	/* coords to set */
{
    register int i;
    float textx, texty, height;	/* for computing vertical text */
    char numstr[10];

    if (!*xo){
	if(cg.ypreci2 == -1){
	    sprintf(numstr, "%d", (int)cg.gmaxy);
	    i = strlen(numstr);
	}else
	    i = cg.ypreci1 + cg.ypreci2;
	*xo = cg.gframe.frame1x + ycharsz[cg.ylabel.t_size] + /* set origins */
		i * ycharsz[cg.ytick.t_size];
    }
    if (!*yo)
	*yo = cg.gframe.frame1y + (3.0 * ycharsz[cg.xlabel.t_size]);
    if(!cg.xlabel.t_xpos)
      cg.xlabel.t_xpos = (cg.xplotmax - *xo)/2 + *xo;
    if(!cg.xlabel.t_ypos)
      cg.xlabel.t_ypos = *yo - (1.75 * ycharsz[cg.xlabel.t_size]);
    if(!cg.yvert){
	if(!cg.ylabel.t_xpos)
	  cg.ylabel.t_xpos = cg.gframe.frame1x+(2*ycharsz[cg.ylabel.t_size]);
	if(!cg.ylabel.t_ypos)
	  cg.ylabel.t_ypos = cg.ycenter;
    }else{
	i = strlen(cg.ylabel.t_text) * ycharsz[cg.ylabel.t_size]; /* get length */
	height = YPLOTMAX-*yo;
	texty = *yo + height - ((height - (float)i)/2.0);
	if(!cg.ylabel.t_xpos)
	  cg.ylabel.t_xpos = cg.gframe.frame1x+ycharsz[cg.ylabel.t_size];
	if(!cg.ylabel.t_ypos)
	  cg.ylabel.t_ypos = texty;
    }
    if (debug)
	fprintf (stderr, "orgs %f %f\n", *xo, *yo);
}
