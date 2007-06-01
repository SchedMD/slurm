#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "mpi.h"
//#include "mpe.h"
#include "pmandel.h"
#include "args.h"

/* I used the line:
gcc -P -E pm_genproc.c | sed 's/{[]*{/{@{/' | tr "@" "\n" | indent -st > pm_genproc_cleanedup.c
to clean this up and see what it looked like.  Eek!
*/

/* hope this doesn't cause anybody problems */
double drand48();

#define DISP(a, b) (int)((char *)(&(a)) - (char *)(&(b)))

int DefineMPITypes()
{
	Flags flags;
	rect rectangle;
	MPI_Aint a, b;
	
	int len[3];
	MPI_Aint disp[3];
	MPI_Datatype types[3];
	
	NUM_type = MPI_DOUBLE;
	
	MPI_Type_contiguous(6, MPI_INT, &winspecs_type);
	MPI_Type_commit(&winspecs_type);
	
	len[0] = 10;
	len[1] = 2;
	len[2] = 6;
	MPI_Address((void*)&flags.breakout, &a);
	MPI_Address((void*)&flags, &b);
	disp[0] = a - b;
	MPI_Address((void*)&flags.boundary_sq, &a);
	disp[1] = a - b;
	MPI_Address((void*)&flags.rmin, &a);
	disp[2] = a - b;
	types[0] = MPI_INT;
	types[1] = MPI_DOUBLE;
	types[2] = NUM_type;
	MPI_Type_struct(3, len, disp, types, &flags_type);
	MPI_Type_commit(&flags_type);
	
	len[0] = 5;
	MPI_Address((void*)&rectangle.l, &a);
	MPI_Address((void*)&rectangle, &b);
	disp[0] = a - b;
	types[0] = MPI_INT;
	MPI_Type_struct(1, len, disp, types, &rect_type);
	MPI_Type_commit(&rect_type);
	
	return 0;
}


int GetDefaultWinspecs(Winspecs *winspecs)
{
	winspecs->height = DEF_height;
	winspecs->width  = DEF_width;
	winspecs->bw     = DEF_bw;
	winspecs->xpos   = DEF_xpos;
	winspecs->ypos   = DEF_ypos;
	winspecs->numColors = DEF_numColors;
	return 0;
}

int GetDefaultFlags(Winspecs *winspecs, Flags *flags)
{
	flags->logfile   = DEF_logfile;
	flags->inf       = DEF_inf;
	flags->outf      = DEF_outf;
	flags->winspecs  = winspecs;
	flags->breakout  = DEF_breakout;
	flags->randomize = DEF_randomize;
	flags->colReduceFactor = DEF_colReduceFactor;
	flags->loop      = DEF_loop;
	flags->zoom      = DEF_zoom;
	flags->askNeighbor = DEF_askNeighbor;
	flags->sendMasterComplexity = DEF_sendMasterComplexity;
	flags->drawBlockRegion = DEF_drawBlockRegion;
	flags->fractal   = DEF_fractal;
	flags->maxiter   = DEF_maxiter;
	flags->boundary_sq = DEF_boundary * DEF_boundary;
	flags->epsilon   = DEF_epsilon;
	NUM_ASSIGN(flags->rmin, DEF_rmin);
	NUM_ASSIGN(flags->rmax, DEF_rmax);
	NUM_ASSIGN(flags->imin, DEF_imin);
	NUM_ASSIGN(flags->imax, DEF_imax);
	NUM_ASSIGN(flags->julia_r, DEF_julia_r);
	NUM_ASSIGN(flags->julia_i, DEF_julia_i);
	return 0;
}

int GetWinspecs(int *argc, char **argv, Winspecs *winspecs)
{
	int myid;
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	if (!myid) 
	{
		GetIntArg(argc, argv, "-height", &(winspecs->height));
		GetIntArg(argc, argv, "-width",  &(winspecs->width));
		winspecs->bw = IsArgPresent(argc, argv, "-bw");
		GetIntArg(argc, argv, "-xpos", &(winspecs->xpos));
		GetIntArg(argc, argv, "-ypos", &(winspecs->ypos));
		GetIntArg(argc, argv, "-colors", &(winspecs->numColors));
	}
	
	MPI_Bcast(winspecs, 1, winspecs_type, 0, MPI_COMM_WORLD);
	return 0;
}

int GetFlags(int *argc, char **argv, Winspecs *winspecs, Flags *flags)
{
	double x, y;
	int myid, strLens[3];
	
	MPI_Comm_rank(MPI_COMM_WORLD, &myid);
	
	if (myid == 0) 
	{
		GetStringArg(argc, argv, "-l", &(flags->logfile));
		GetStringArg(argc, argv, "-i", &(flags->inf));
		/* if reading from input file, disable zooming */
		if (flags->inf) 
		{
			flags->zoom = 0;
		}
		GetStringArg(argc, argv, "-o", &(flags->outf));
		GetIntArg(argc, argv, "-breakout", &(flags->breakout));
		if (IsArgPresent(argc, argv, "-randomize")) 
		{
			flags->randomize = 0;
		}
		if (IsArgPresent(argc, argv, "+randomize")) 
		{
			flags->randomize = 1;
		}
		GetIntArg(argc, argv, "-colreduce", &(flags->colReduceFactor));
		flags->loop = IsArgPresent(argc, argv, "-loop");
		if (IsArgPresent(argc, argv, "-zoom")) 
		{
			flags->zoom = 0;
		}
		if (IsArgPresent(argc, argv, "+zoom") && !flags->inf) 
		{
			flags->zoom = 1;
		}
		flags->askNeighbor = IsArgPresent(argc, argv, "-neighbor");
		flags->sendMasterComplexity = IsArgPresent(argc, argv, "-complexity");
		flags->drawBlockRegion = IsArgPresent(argc, argv, "-delaydraw");
		
		if (IsArgPresent(argc, argv, "-mandel")) 
		{
			flags->fractal = MBROT;
		} 
		else if (IsArgPresent(argc, argv, "-julia")) 
		{
			flags->fractal = JULIA;
		} 
		else if (IsArgPresent(argc, argv, "-newton")) 
		{
			flags->fractal = NEWTON;
		}
		
		GetIntArg(argc, argv, "-maxiter", &(flags->maxiter));
		if (GetDoubleArg(argc, argv, "-boundary", &x)) 
		{
			flags->boundary_sq = x * x;
		}
		GetDoubleArg(argc, argv, "-epsilon", &(flags->epsilon));
		if (GetDoubleArg(argc, argv, "-rmin", &x)) 
		{
			NUM_ASSIGN(flags->rmin, DBL2NUM(x));
		}
		if (GetDoubleArg(argc, argv, "-rmax", &x)) 
		{
			NUM_ASSIGN(flags->rmax, DBL2NUM(x));
		}
		
		if (GetDoubleArg(argc, argv, "-imin", &x)) 
		{
			NUM_ASSIGN(flags->imin, DBL2NUM(x));
		}
		if (GetDoubleArg(argc, argv, "-imax", &x)) 
		{
			NUM_ASSIGN(flags->imax, DBL2NUM(x));
		}
		if (GetDoubleArg(argc, argv, "-radius", &x)) 
		{
			if (GetDoubleArg(argc, argv, "-rcenter", &y)) 
			{
				NUM_ASSIGN(flags->rmin, DBL2NUM(y-x));
				NUM_ASSIGN(flags->rmax, DBL2NUM(y + x));
			}
			if (GetDoubleArg(argc, argv, "-icenter", &y)) 
			{
				NUM_ASSIGN(flags->imin, DBL2NUM(y-x));
				NUM_ASSIGN(flags->imax, DBL2NUM(y + x));
			}
		}
		strLens[0] = (flags->logfile) ? strlen(flags->logfile) + 1 : 0;
		strLens[1] = (flags->inf)     ? strlen(flags->inf) + 1     : 0;
		strLens[2] = (flags->outf)    ? strlen(flags->outf) + 1    : 0;
	} /* End of myid == 0 */
	
	MPI_Bcast(flags, 1, flags_type, 0, MPI_COMM_WORLD);
	
	MPI_Bcast(strLens, 3, MPI_INT, 0, MPI_COMM_WORLD);
	
	if (myid != 0) 
	{
		flags->logfile = (strLens[0]) ?
			(char *)malloc(strLens[0] * sizeof(char)) : 0;
		flags->inf = (strLens[1]) ?
			(char *)malloc(strLens[1] * sizeof(char)) : 0;
		flags->outf = (strLens[2]) ?
			(char *)malloc(strLens[2] * sizeof(char)) : 0;
	}
	if (strLens[0]) 
		MPI_Bcast(flags->logfile, strLens[0], MPI_CHAR, 0, MPI_COMM_WORLD);
	if (strLens[1]) 
		MPI_Bcast(flags->inf,     strLens[1], MPI_CHAR, 0, MPI_COMM_WORLD);
	if (strLens[2]) 
		MPI_Bcast(flags->outf,    strLens[2], MPI_CHAR, 0, MPI_COMM_WORLD);
	
	return 0;
}



int Pixel2Complex(Flags *flags, int x, int y, NUM *nx, NUM *ny)
{
	NUM_PTR_ASSIGN(
		nx,
		NUM_ADD(
		NUM_MULT(
		DBL2NUM((double)x / flags->winspecs->width),
		NUM_SUB(flags->rmax, flags->rmin)),
		flags->rmin)
		);
	
	NUM_PTR_ASSIGN(
		ny,
		NUM_ADD(
		NUM_MULT(
        DBL2NUM((double)y / flags->winspecs->height),
        NUM_SUB(flags->imin, flags->imax)),
		flags->imax)
		);
	
		/*
		fprintf(stderr, "In (%d %d) to (%lf,%lf)-(%lf,%lf)\n",
		flags->winspecs->width, flags->winspecs->height,
		flags->rmin, flags->imin, flags->rmax, flags->imax);
		fprintf(stderr, "Converted (%d, %d) to (%lf, %lf)\n",
		x, y, *nx, *ny);
	*/
	
	return 0;
}


int StrContainsNonWhiteSpace(char *str)
{
	while (*str) 
	{
		if (!isspace(*str)) return 1;
		str++;
	}
	return 0;
}




/* Q_Create - create the queue */
void Q_Create(rect_queue *q, int randomize)
{
	q->head = q->tail = 0;       /* create the queue */
	q->size = 100;
	q->r = (rect *) malloc(q->size * sizeof(rect));
	q->randomPt = 1;
	q->randomize = randomize;
}


/* Q_Checksize - check if the queue is full.  If so, double the size */
void Q_Checksize(rect_queue *q)
{
	if (q->head == q->tail + 1 ||
		!q->head && q->tail == q->size - 1) 
	{
		/* if the queue is full */
		q->r = (rect *) realloc(q->r, sizeof(rect) * q->size * 2);
		/* get a bigger queue */
		if (q->tail < q->head) 
		{
			memcpy(q->r + q->size, q->r, q->tail * sizeof(rect));
			/* copy over any data that needs to be moved */
			q->tail += q->size;
		}
		if (q->randomize && q->randomPt<q->head) 
		{
			q->randomPt += q->size;
		}
		q->size *= 2;
	}
}


void Q_Print(rect_queue *q)
{
	int i;
	i = q->head;
	while (i != q->tail) 
	{
		fprintf(debug_file, "queue[%d] = (%d %d %d %d)\n", i, q->r[i].l, q->r[i].r,
			q->r[i].t, q->r[i].b);
		i++;
		if (i == q->size) i = 0;
	}
}


int Q_CheckValidity(rect_queue *q)
{
	int i;
	i = q->head;
	while (i != q->tail) 
	{
		if (q->r[i].l > 10000 ||
			q->r[i].r > 10000 ||
			q->r[i].t > 10000 ||
			q->r[i].b > 10000 ||
			q->r[i].length > 10000) 
		{
			fprintf(debug_file, "Error in queue[%d]: (%d %d %d %d %d)\n",
				i, q->r[i].l, q->r[i].r, q->r[i].t, q->r[i].b, q->r[i].length);
		}
		if (++i == q->size) i = 0;
	}
	return 0;
}


/* Q_Enqueue - add a rectangle to the queue */
void Q_Enqueue(rect_queue *q, rect *r)
{
	Q_Checksize(q);
	q->r[q->tail] = *r;
	if (++q->tail == q->size) q->tail = 0;
}

/* Q_Dequeue - remove a rectangle from the queue */
void Q_Dequeue(rect_queue *q, rect *r)
{
	double rand_no;
	*r = q->r[q->head];
	if (++q->head == q->size) q->head = 0;
	if (q->randomize && ((q->head == q->randomPt) ||
		(q->head == q->randomPt + 1))) 
	{
		int i, j, numItems;
		rect temp;
		numItems = (q->tail<q->head)
			? q->size-q->head + q->tail
			: q->tail - q->head;
		for (i = q->head; i != q->tail; i++) 
		{
			rand_no = drand48();
			j = (int)(rand_no * numItems) + q->head;
			if (j >= q->size) j -= q->size;
			temp = q->r[j];
			q->r[j] = q->r[i];
			q->r[i] = temp;
			if (i == q->size-1) 
			{
				i = -1;
			}
		}
		q->randomPt = q->tail;
	}
}




int RectBorderLen(rect *r)
{
	return (r->r-r->l) ?
		(r->b-r->t) ?
		(2 * (r->r-r->l + r->b-r->t))
		:
	(r->r - r->l + 1)
		:
	   (r->b-r->t) ?
		   (r->b - r->t + 1)
		   :
	   1;
}


void PrintHelp(char *progName)
{
	printf("Options recognized by %s:\n", progName);
	printf("(defaults are in parentheses ())\n");
	printf("   -i <filename>              (none) input file\n");
	printf("   -xpos <xpos>               (%d) window horizontal coordinate\n",
		DEF_xpos);
	printf("   -ypos <xpos>               (%d) window vertical coordinate\n",
		DEF_ypos);
	printf("   -width <width>             (%d) width of computed area in points\n", DEF_width);
	printf("   -height <height>           (%d) height of computed area in points\n", DEF_height);
	printf("   -boundary <boundary>       (%.1lf) boundary value for M-set computation\n", DEF_boundary);
	printf("   -maxiter <max. iter>       (%d) maximum # of iterations for M-set\n", DEF_maxiter);
	printf("                              compuptation algorithm\n");
	printf("   -rmin <real min.>          (%.2lf) minimum real coordinate of computed area\n", DEF_rmin);
	printf("   -rmax <real max.>          (%.2lf) maximum real coordinate of computed area\n", DEF_rmax);
	printf("   -imin <imag. min.>         (%.2lf) minimum imaginary coordinate of computed\n", DEF_imin);
	printf("                              area\n");
	printf("   -imax <imag. max.>         (%.2lf) maximum imaginary coordinate of computed\n", DEF_imax);
	printf("                              area\n");
	printf("\n");
	printf("      alternate form: (if specified, overrides <r | i><min | max>)\n");
	printf("   -rcenter <real center>     (%.2lf) center real coordinate of computed area\n", (DEF_rmin + DEF_rmax)/2);
	printf("   -icenter <imag. center>    (%.2lf) center imaginary coordinate of computed\n", (DEF_imin + DEF_imax)/2);
	printf("                              area\n");
	printf("   -radius <area radius>      (%.2lf) radius of the computed area\n", (DEF_rmax - DEF_rmin));
	printf("\n");
	printf("   -breakout <breakout size>  (%d) maximum length or width rectangle to\n", DEF_breakout);
	printf("                              subdivide\n");
	printf("   -colors <# of colors>      (%d) number of colors to request\n", DEF_numColors);
	printf("   -colreduce <reduce factor> (%d) factor by which to scale down iteration\n", DEF_colReduceFactor);
	printf("                              values to reduce color changes\n");
	printf("   <+, ->zoom                  (%s) turn on (off) drag&zoom\n",
		DEF_zoom ? "on" : "off");
	printf("   <+, ->randomize             (%sset) (on, off) compute regions in as random of\n",
		DEF_randomize ? "" : "not ");
	printf("                              order as possible\n");
	printf("   -bw                        (%sset) draw in black and white instead of\n", DEF_bw ? "" : "not ");
	printf("                              color\n");
	exit(0);
}


MPE_Color Iter2Color(Flags *flags, int iter)
{
	if (flags->winspecs->bw) 
	{
		return ((iter == flags->maxiter) ? MPE_BLACK :
		((iter / flags->colReduceFactor) % 2) ? MPE_WHITE : MPE_BLACK);
	} 
	else 
	{
		if (iter == flags->maxiter) 
		{
			return MPE_BLACK;
		} 
		else 
		{
			return flags->winspecs->colorArray[(iter / flags->colReduceFactor) %
				flags->winspecs->numColors];
		}
	}
}


void ChunkIter2Color(Flags *flags, int *iterData, int *colorData, int size)
{
	int i;
	
	for (i = 0; i<size; i++) 
	{
		*colorData = Iter2Color(flags, *iterData);
		colorData++;
		iterData++;
	}
}




int ComputeChunk(Flags *flags, rect *r, MPE_Point *pointData, int *iterData, int maxnpoints, int *npoints)
{
	int i, x, y;
	CalcField(flags->fractal, iterData, r->l, r->r, r->t, r->b);
    /* compute the field */
	
	*npoints = (r->r - r->l + 1) * (r->b - r->t + 1);
	x = r->l;  y = r->t;
	for (i = 0; i<*npoints; i++) 
	{
		pointData[i].x = x++;
		pointData[i].y = y;
		pointData[i].c = Iter2Color(flags, iterData[i]);
		if (x > r->r) 
		{
			x = r->l;
			y++;
		}
	}
	return 0;
}


/*
int DrawChunk(MPE_XGraph graph, MPE_Color *colorData, rect r)
{
	int a, b;
	
	for (b = r.t; b <= r.b; b++) 
	{
		for (a = r.l; a <= r.r; a++) 
		{
			MPE_Draw_point(graph, a, b, *colorData);
			colorData++;
		}
	}
	MPE_Update(graph);
	return 0;
}
//*/

#define LOOP(start, toContinue, incrBefore, fn, check, lbl, incrAfter) \
	start; \
while (toContinue) \
{ \
incrBefore; \
pointPtr->x = x; \
pointPtr->y = y; \
pointPtr->c = Iter2Color(flags, fn(re, im)); \
check \
lbl \
incrAfter; \
}

/*
fprintf(stderr, "computed (%d %d) to be %d\n", x, y, pointPtr->c); \
*/

/* really, all these stupid loop macros will make it easier! */

#define LOOP_TOP(fn, check, lbl) \
	LOOP((y = r.t, x = r.l + 1), x <= r.r, NUM_ASSIGN(re, NUM_ADD(re, rstep)), \
fn, check, lbl, (pointPtr++, x++));

#define LOOP_RIGHT(fn, check, lbl) \
	LOOP((x = r.r, y = r.t + 1), y <= r.b, NUM_ASSIGN(im, NUM_ADD(im, istep)), \
fn, check, lbl, (pointPtr++, y++));

#define LOOP_BOTTOM(fn, check, lbl) \
	LOOP((y = r.b, x = r.r-1), x >= r.l, NUM_ASSIGN(re, NUM_SUB(re, rstep)), \
fn, check, lbl, (pointPtr++, x--));

#define LOOP_LEFT(fn, check, lbl) \
	LOOP((x = r.l, y = r.b-1), y>r.t,  NUM_ASSIGN(im, NUM_SUB(im, istep)), \
fn, check, lbl, (pointPtr++, y--));

#define LOOP_TOP_CHECK(fn, lbl) \
LOOP_TOP(fn, if (pointPtr->c != firstColor) goto lbl; ,; );

#define LOOP_RIGHT_CHECK(fn, lbl) \
LOOP_RIGHT(fn, if (pointPtr->c != firstColor) goto lbl; ,; );

#define LOOP_BOTTOM_CHECK(fn, lbl) \
LOOP_BOTTOM(fn, if (pointPtr->c != firstColor) goto lbl; ,; );

#define LOOP_LEFT_CHECK(fn, lbl) \
LOOP_LEFT(fn, if (pointPtr->c != firstColor) goto lbl; ,; );

#define LOOP_TOP_NOCHECK(fn, lbl) \
LOOP_TOP(fn,; , lbl:);

#define LOOP_RIGHT_NOCHECK(fn, lbl) \
LOOP_RIGHT(fn,; , lbl:);

#define LOOP_BOTTOM_NOCHECK(fn, lbl) \
LOOP_BOTTOM(fn,; , lbl:);

#define LOOP_LEFT_NOCHECK(fn, lbl) \
LOOP_LEFT(fn,; , lbl:);

#define LOOP_FN(fn, lbl1, lbl2, lbl3, lbl4) \
if (r.b-r.t>1 && r.r-r.l>1) \
{ \
/* if there's a chance to subdivide, */ \
LOOP_TOP_CHECK(fn, lbl1); \
LOOP_RIGHT_CHECK(fn, lbl2); \
LOOP_BOTTOM_CHECK(fn, lbl3); \
LOOP_LEFT_CHECK(fn, lbl4); \
*isContinuous = 1; \
return 1;   /* if we made it to this point, it's continuous */ \
LOOP_TOP_NOCHECK(fn, lbl1); \
LOOP_RIGHT_NOCHECK(fn, lbl2); \
LOOP_BOTTOM_NOCHECK(fn, lbl3); \
LOOP_LEFT_NOCHECK(fn, lbl4); \
*isContinuous = 0; \
return 0;   /* it ain't continuous */ \
} \
else \
{ /* if there's no chance to subdivide, don't insert the checks */ \
LOOP_TOP(fn,; ,; ); \
LOOP_RIGHT(fn,; ,; ); \
if (r.r-r.l && r.b-r.t) \
{ \
/* only do the opposite sides if >1 row and >1 column */ \
LOOP_BOTTOM(fn,; ,; ); \
LOOP_LEFT(fn,; ,; ); \
} \
*isContinuous = 0; \
return 0;  /* it may or may not be continuous, doesn't matter */ \
}



int ComputeBorder(Winspecs *winspecs, Flags *flags, rect *rectPtr, MPE_Point *pointData, int maxnpoints,
				  int *npoints, int *isContinuous)
{
	register NUM re, im, rstep, istep;
	register int x, y;
	register MPE_Point *pointPtr;
	register MPE_Color firstColor;
	rect r;
	
	r = *rectPtr;
	/* xsplit, ysplit - where to split the rectangle */
	
    /* set the complex points */
	NUM_ASSIGN(re, COORD2CMPLX(flags->rmin, flags->rmax, 0,
		winspecs->width-1,  r.l));
	NUM_ASSIGN(im, COORD2CMPLX(flags->imax, flags->imin, 0,
		winspecs->height-1, r.t));
	NUM_ASSIGN(rstep,  NUM_DIV(NUM_SUB(flags->rmax, flags->rmin),
		INT2NUM(winspecs->width-1)));
	NUM_ASSIGN(istep,  NUM_DIV(NUM_SUB(flags->imin, flags->imax),
		INT2NUM(winspecs->height-1)));
	
	pointPtr = pointData + 1;
	pointData->x = r.l;
	pointData->y = r.t;
	pointData->c = firstColor = Iter2Color(flags,
		(flags->fractal == MBROT) ? MbrotCalcIter(re, im) :
	(flags->fractal == JULIA) ? JuliaCalcIter(re, im) :
	MbrotCalcIter(re, im));
	
	*npoints = r.length;
    /* calculate first point */
	
	
	switch (flags->fractal) 
	{
	case MBROT:
		LOOP_FN(MbrotCalcIter, m1, m2, m3, m4);
	case JULIA:
		LOOP_FN(JuliaCalcIter, j1, j2, j3, j4);
	case NEWTON:
		LOOP_FN(MbrotCalcIter, n1, n2, n3, n4);
	}
	return 0;
}

/*
void DrawBorder(MPE_XGraph graph, MPE_Color *colorData, rect r)
{
	int x, y;
	
	for (y = r.t, x = r.l; x <= r.r; x++) 
	{
		MPE_Draw_point(graph, x, y, *colorData);
		colorData++;
	}
	for (x = r.r, y = r.t + 1; y <= r.b; y++) 
	{
		MPE_Draw_point(graph, x, y, *colorData);
		colorData++;
	}
	if (r.r-r.l && r.b-r.t) 
	{
		for (y = r.b, x = r.r-1; x >= r.l; x--) 
		{
			MPE_Draw_point(graph, x, y, *colorData);
			colorData++;
		}
		for (x = r.l, y = r.b-1; y>r.t; y--) 
		{
			MPE_Draw_point(graph, x, y, *colorData);
			colorData++;
		}
	}
	MPE_Update(graph);
}
*/

void DrawBlock(MPE_XGraph graph, MPE_Point *pointData, rect *r)
{
	//printf("block color: %d\n", pointData->c);
	MPE_Fill_rectangle(graph, r->l, r->t, r->r - r->l + 1, r->b - r->t + 1,
		      pointData->c);
	
	MPE_Update(graph);
}

