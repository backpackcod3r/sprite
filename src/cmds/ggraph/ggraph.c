#include <stdio.h>
#include <errno.h>
#include <strings.h>
#include <math.h>
#include <ctype.h>
#include "ggraph.h"
#include "ggraphdefs.h"

int interact;			/* switch for interactive mode */
main (argc, argv)
char   *argv[];
int     argc;
{
FILE *infile;			/* input file */

    if(argc == 3) {		/* scan arguments */
      if(!strcmp(argv[1], "-d")){
	debug++;
	argc--, argv++;
      }
      if(!strcmp(argv[1], "-i")){
	interact++;
	argc--, argv++;
      }
    }
    if (argc == 1) {
	infile = stdin;
	fflush(stdin);
	outfile = stdout;
    }else
    if ((infile = fopen (argv[1], "r")) == NULL) {/* open input file */
	perror ("can't open input file");
	exit (-1);
    }
    if ((outfile = fopen (argv[2], "w")) != NULL)/* open output file */
	setbuf (outfile, NULL);	/* no buffering */
    if(debug)fprintf (stderr, "Reading file\n");
    readcmds (infile);		/* read graph commands */
    if(debug)fprintf (stderr, "Graph program done\n");
    exit (0);
}

/****************************************************************
 *								*
 *	gremlinheader - write header for gremlin file		*
 *								*
 ****************************************************************/
gremlinheader () {
    if(version == SUN_GREMLIN)
      fprintf (outfile, "%s\n%d %4.1f %4.1f\n",
	    sfirstline, VERTICAL, cg.xorigin, cg.yorigin);
    else
      fprintf (outfile, "%s\n%d %4.1f %4.1f\n",
	    firstline, VERTICAL, cg.xorigin, cg.yorigin);
}

/****************************************************************
 *								*
 *	drawctext - write some text 				*
 *		    x, y - point to draw text at		*
 *		    font - font to use				*
 *		    size - size of text				*
 *		    string - text to draw			*
 *		    just - justification to use			*
 *								*
 ****************************************************************/
drawctext (x, y, font, size, string, just)
int     font;
int     size;
float   x,
        y;
char   *string;
int     just;
{
    float pos_x, pos_y;
    int length;

    length = strlen(string) * xcharsz[size];

    switch (just) {
	case BOTLEFT_TEXT:
	    pos_x = x;
	    pos_y = y;
	case BOTCENTER_TEXT:
	    pos_x = x - (length >> 1);
	    pos_y = y;
	    break;
	case BOTRIGHT_TEXT:
	    pos_x = x - length;
	    pos_y = y;
	    break;
	case CENTERLEFT_TEXT:
	    pos_x = x;
	    pos_y = y - (ycharsz[size] >> 1);
	    break;
	case CENTERCENTER_TEXT:
	    pos_x = x - (length >> 1);
	    pos_y = y - (ycharsz[size] >> 1);
	    break;
	case CENTERRIGHT_TEXT:
	    pos_x = x - length;
	    pos_y = y - (ycharsz[size] >> 1);
	    break;
	case TOPLEFT_TEXT:
	    pos_x = x;
	    pos_y = y + descenders[size] - ycharsz[size];
	    break;
	case TOPCENTER_TEXT:
	    pos_x = x - (length >> 1);
	    pos_y = y + descenders[size] - ycharsz[size];
	    break;
	case TOPRIGHT_TEXT:
	    pos_x = x - length;
	    pos_y = y + descenders[size] - ycharsz[size];
	    break;
    }

    if (version == SUN_GREMLIN)
	fprintf(outfile, "%s\n", justify_names[just]);
    else
	fprintf(outfile, "%d\n", just);

    fprintf(outfile, "%3.2f %3.2f\n", x, y);
    fprintf(outfile, "%3.2f %3.2f\n", pos_x, pos_y);
    fprintf(outfile, "%3.2f %3.2f\n", pos_x + (length >> 1), pos_y);
    fprintf(outfile, "%3.2f %3.2f\n", pos_x + length, pos_y);

    fprintf(outfile, (version == SUN_GREMLIN) ? "*\n" : "-1.00 -1.00\n");
    fprintf (outfile, "%d %d\n%d %s\n", font, size, strlen (string), string);
    return(GR_OK);
}

/****************************************************************
 *								*
 *	drawvtext - write some vertical text			*
 *		    x, y - point to draw text at		*
 *		    font - font to use				*
 *		    size - size of text				*
 *		    string - text to draw			*
 *		    charh - height per character to use		*
 *								*
 ****************************************************************/
drawvtext (x, y, font, size, string, charh)
int     font;
int     size;
float   x, y;
char   *string;
int   charh;
{
    char *cp;			/* pointer into string */

    cp = string;
    while(*cp){
    if (version == SUN_GREMLIN)
	fprintf(outfile, "%s\n", justify_names[TOPCENTER_TEXT]);
    else
	fprintf(outfile, "%d\n",  TOPCENTER_TEXT);
	fprintf (outfile, "%4.1f %4.1f\n", x, y);
	fprintf (outfile, "%4.1f %4.1f\n", x, y);
	fprintf (outfile, "%4.1f %4.1f\n", x + 5.0, y);
	fprintf (outfile, "%4.1f %4.1f\n", x + 10.0, y);
	fprintf(outfile, (version == SUN_GREMLIN) ? "*\n" : "-1.00 -1.00\n");
	fprintf (outfile, "%d %d\n%d %c\n", font, size, 1, *cp);
	++cp;
	y = y - charh;
    }
}

/****************************************************************
 *								*
 *	drawline - write a line 				*
 *		    x1, y1 - starting point			*
 *		    x1, y1 - ending point			*
 *								*
 ****************************************************************/
drawline (brush, x1, y1, x2, y2)
int     brush;
float   x1, x2, y1, y2;
{
    if(version == SUN_GREMLIN)
      fprintf (outfile, "VECTOR\n%4.1f %4.1f\n%4.1f %4.1f\n", x1, y1, x2, y2);
    else
      fprintf (outfile, "%d\n%4.1f %4.1f\n%4.1f %4.1f\n", LINE, x1, y1, x2, y2);
    fprintf(outfile, (version == SUN_GREMLIN) ? "*\n" : "-1.00 -1.00\n");
    fprintf (outfile, "%d %d\n%d\n", brush, 0, 0);
}

