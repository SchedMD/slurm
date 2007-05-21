



















































typedef char *__gnuc_va_list;



























extern struct _iobuf {
  int _cnt;
  unsigned char *_ptr;
  unsigned char *_base;
  int _bufsiz;
  short _flag;
  char _file;
} _iob[];































extern struct _iobuf *fopen(const char *, const char *);
extern struct _iobuf *fdopen(int, const char *);
extern struct _iobuf *freopen(const char *, const char *, struct _iobuf *);
extern struct _iobuf *popen(const char *, const char *);
extern struct _iobuf *tmpfile();
extern long ftell(struct _iobuf *);
extern char *fgets(char *, int, struct _iobuf *);
extern char *gets(char *);
extern char *sprintf(char *, const char *,...);
extern char *ctermid(char *);
extern char *cuserid(char *);
extern char *tempnam(const char *, const char *);
extern char *tmpnam(char *);










typedef enum _Fractal_type {
  MBROT, JULIA, NEWTON
} Fractal_type;








































void Mbrot_Settings(double boundary, int maxiter);
int MbrotCalcIter(double re, double im);
void Julia_Settings(double boundary, int maxiter, double real, double
		     imag);
int JuliaCalcIter(double re, double im);
void Mbrotrep_Settings(double boundary, int maxiter, int miniter,
		        int longestCycle, double fudgeFactor);
int MbrotrepCalcIter(double re, double im);

void CalcField(Fractal_type, int *iterField,
	        int xstart, int xend, int ystart, int yend);

void Copysub2DArray(int *mainArray, int *subArray, int mainWidth,
		     int mainHeight, int subWidth, int subHeight,
		     int xpos, int ypos);

typedef struct Mbrot_settings_ {
  double boundary_sq;

  int maxiter;
} Mbrot_settings;

typedef struct Julia_settings_ {
  double boundary_sq;
  int maxiter;
  double r, i;
} Julia_settings;

typedef struct Newton_settings_ {
  double epsilon;

  int *coeff;
  int nterms;
  int maxiter;

} Newton_settings;









extern MPI_Datatype winspecs_type, flags_type, NUM_type, rect_type;
extern struct _iobuf *debug_file;

typedef enum _Algorithms {
  alg_block,
  alg_separate_rect,
  alg_solid_rect
} Alogrithms;

typedef struct _Winspecs {
  int height, width;
  int bw;
  int xpos, ypos;
  int numColors;
  MPE_Color *colorArray;
} Winspecs;

typedef struct _Flags {
  char *logfile;
  char *inf;
  char *outf;
  Winspecs *winspecs;
  int breakout;
  int randomize;
  int colReduceFactor;

  int loop;
  int zoom;
  int askNeighbor;

  int sendMasterComplexity;


  int drawBlockRegion;


  int fractal;
  int maxiter;
  double boundary_sq;
  double epsilon;
  double rmin, rmax, imin, imax;
  double julia_r, julia_i;
} Flags;




















































typedef struct {
  int l, r, t, b, length;

} rect;

typedef struct {
  int head, tail, size, randomPt, randomize;
  rect *r;
} rect_queue;











































DefineMPITypes()
{
  Winspecs winspecs;
  Flags flags;
  rect rectangle;

  int len[3], disp[3];
  MPI_Datatype types[3];

  NUM_type = MPI_DOUBLE;

  MPI_Type_contiguous(6, MPI_INT, &winspecs_type);
  MPI_Type_commit(&winspecs_type);

  len[0] = 10;
  len[1] = 2;
  len[2] = 6;
  disp[0] = (int) ((char *) (&(flags.breakout)) - (char *) (&(flags)));
  disp[1] = (int) ((char *) (&(flags.boundary_sq)) - (char *) (&(flags)));
  disp[2] = (int) ((char *) (&(flags.rmin)) - (char *) (&(flags)));
  types[0] = MPI_INT;
  types[1] = MPI_DOUBLE;
  types[2] = NUM_type;
  MPI_Type_struct(3, len, disp, types, &flags_type);
  MPI_Type_commit(&flags_type);

  len[0] = 5;
  disp[0] = (int) ((char *) (&(rectangle.l)) - (char *) (&(rectangle)));
  types[0] = MPI_INT;
  MPI_Type_struct(1, len, disp, types, &rect_type);
  MPI_Type_commit(&rect_type);

  return 0;
}


GetDefaultWinspecs(winspecs)
Winspecs *winspecs;
{
  winspecs->height = 500;
  winspecs->width = 500;
  winspecs->bw = 0;
  winspecs->xpos = -1;
  winspecs->ypos = -1;
  winspecs->numColors = 16;

  return 0;
}
GetDefaultFlags(winspecs, flags)
Winspecs *winspecs;
Flags *flags;
{
  flags->logfile = 0;
  flags->inf = 0;
  flags->outf = 0;
  flags->winspecs = winspecs;
  flags->breakout = 12;
  flags->randomize = 1;
  flags->colReduceFactor = 4;
  flags->loop = 0;
  flags->zoom = 1;
  flags->askNeighbor = 1;
  flags->sendMasterComplexity = 0;
  flags->drawBlockRegion = 1;
  flags->fractal = MBROT;
  flags->maxiter = 1000;
  flags->boundary_sq = 2.0 * 2.0;
  flags->epsilon = .01;
  {
    (flags->rmin) = (-2.0);
  };
  {
    (flags->rmax) = (2.0);
  };
  {
    (flags->imin) = (-2.0);
  };
  {
    (flags->imax) = (2.0);
  };
  {
    (flags->julia_r) = (.331);
  };
  {
    (flags->julia_i) = (-.4);
  };

  return 0;
}
GetWinspecs(argc, argv, winspecs)
int *argc;
char **argv;
Winspecs *winspecs;
{
  int myid;

  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (!myid) {
    GetIntArg(argc, argv, "-height", &(winspecs->height));
    GetIntArg(argc, argv, "-width", &(winspecs->width));
    winspecs->bw = IsArgPresent(argc, argv, "-bw");
    GetIntArg(argc, argv, "-xpos", &(winspecs->xpos));
    GetIntArg(argc, argv, "-ypos", &(winspecs->ypos));
    GetIntArg(argc, argv, "-colors", &(winspecs->numColors));
  }
  MPI_Bcast(winspecs, 1, winspecs_type, 0, MPI_COMM_WORLD);



  return 0;
}
GetFlags(argc, argv, winspecs, flags)
int *argc;
char **argv;
Winspecs *winspecs;
Flags *flags;
{
  double x, y;
  int myid, strLens[3];

  MPI_Comm_rank(MPI_COMM_WORLD, &myid);

  if (!myid) {
    GetStringArg(argc, argv, "-l", &(flags->logfile));
    GetStringArg(argc, argv, "-i", &(flags->inf));
    GetStringArg(argc, argv, "-o", &(flags->outf));
    GetIntArg(argc, argv, "-breakout", &(flags->breakout));
    if (IsArgPresent(argc, argv, "-randomize")) {
      flags->randomize = 0;
    }
    if (IsArgPresent(argc, argv, "+randomize")) {
      flags->randomize = 1;
    }
    GetIntArg(argc, argv, "-colreduce", &(flags->colReduceFactor));
    flags->loop = IsArgPresent(argc, argv, "-loop");
    if (IsArgPresent(argc, argv, "-zoom")) {
      flags->zoom = 0;
    }
    if (IsArgPresent(argc, argv, "+zoom")) {
      flags->zoom = 1;
    }
    flags->askNeighbor = IsArgPresent(argc, argv, "-neighbor");
    flags->sendMasterComplexity = IsArgPresent(argc, argv, "-complexity");
    flags->drawBlockRegion = IsArgPresent(argc, argv, "-delaydraw");

    if (IsArgPresent(argc, argv, "-mandel")) {
      flags->fractal = MBROT;
    } else if (IsArgPresent(argc, argv, "-julia")) {
      flags->fractal = JULIA;
    } else if (IsArgPresent(argc, argv, "-newton")) {
      flags->fractal = NEWTON;
    }
    GetIntArg(argc, argv, "-maxiter", &(flags->maxiter));
    if (GetDoubleArg(argc, argv, "-boundary", &x)) {
      flags->boundary_sq = x * x;
    }
    GetDoubleArg(argc, argv, "-epsilon", &(flags->epsilon));
    if (GetDoubleArg(argc, argv, "-rmin", &x)) {
      {
	(flags->rmin) = ((x));
      };
    }
    if (GetDoubleArg(argc, argv, "-rmax", &x)) {
      {
	(flags->rmax) = ((x));
      };
    }
    if (GetDoubleArg(argc, argv, "-imin", &x)) {
      {
	(flags->imin) = ((x));
      };
    }
    if (GetDoubleArg(argc, argv, "-imax", &x)) {
      {
	(flags->imax) = ((x));
      };
    }
    if (GetDoubleArg(argc, argv, "-radius", &x)) {
      if (GetDoubleArg(argc, argv, "-rcenter", &y)) {
	{
	  (flags->rmin) = ((y - x));
	};
	{
	  (flags->rmax) = ((y + x));
	};
      }
      if (GetDoubleArg(argc, argv, "-icenter", &y)) {
	{
	  (flags->imin) = ((y - x));
	};
	{
	  (flags->imax) = ((y + x));
	};
      }
    }
    strLens[0] = (flags->logfile) ? strlen(flags->logfile) + 1 : 0;
    strLens[1] = (flags->inf) ? strlen(flags->inf) + 1 : 0;
    strLens[2] = (flags->outf) ? strlen(flags->outf) + 1 : 0;
  }
  MPI_Bcast(flags, 1, flags_type, 0, MPI_COMM_WORLD);

  MPI_Bcast(strLens, 3, MPI_INT, 0, MPI_COMM_WORLD);
  if (myid != 0) {
    flags->logfile = (strLens[0]) ?
       (char *) malloc(strLens[0] * sizeof(char)) : 0;
    flags->inf = (strLens[1]) ?
       (char *) malloc(strLens[1] * sizeof(char)) : 0;
    flags->outf = (strLens[2]) ?
       (char *) malloc(strLens[2] * sizeof(char)) : 0;
  }
  if (strLens[0])
    MPI_Bcast(flags->logfile, strLens[0], MPI_CHAR, 0, MPI_COMM_WORLD);
  if (strLens[1])
    MPI_Bcast(flags->inf, strLens[1], MPI_CHAR, 0, MPI_COMM_WORLD);
  if (strLens[2])
    MPI_Bcast(flags->outf, strLens[2], MPI_CHAR, 0, MPI_COMM_WORLD);


  return 0;
}



Pixel2Complex(flags, x, y, nx, ny)
Flags *flags;
int x, y;
double *nx, *ny;
{
  {
    *(
      nx) = (
	     ((
	       ((
		 ((double) x / flags->winspecs->width)) * (
				      ((flags->rmax) - (flags->rmin))))) + (
							       flags->rmin))
       );
  };

  {
    *(
      ny) = (
	     ((
	       ((
		 ((double) y / flags->winspecs->height)) * (
				      ((flags->imin) - (flags->imax))))) + (
							       flags->imax))
       );
  };









  return 0;
}


StrContainsNonWhiteSpace(str)
char *str;
{
  while (*str) {
    if (!isspace(*str))
      return 1;
    str++;
  }
  return 0;
}





void Q_Create(q, randomize)
rect_queue *q;
int randomize;
{
  q->head = q->tail = 0;
  q->size = 100;
  q->r = (rect *) malloc(q->size * sizeof(rect));
  q->randomPt = 1;
  q->randomize = randomize;
}



void Q_Checksize(q)
rect_queue *q;
{
  if (q->head == q->tail + 1 ||
      !q->head && q->tail == q->size - 1) {

    q->r = (rect *) realloc(q->r, sizeof(rect) * q->size * 2);

    if (q->tail < q->head) {
      memcpy(q->r + q->size, q->r, q->tail * sizeof(rect));

      q->tail += q->size;
    }
    if (q->randomize && q->randomPt < q->head) {
      q->randomPt += q->size;
    }
    q->size *= 2;
  }
}


Q_Print(q)
rect_queue *q;
{
  int i;
  i = q->head;
  while (i != q->tail) {
    fprintf(debug_file, "queue[%d] = (%d %d %d %d)\n", i, q->r[i].l, q->r[i].r,
	    q->r[i].t, q->r[i].b);
    i++;
    if (i == q->size)
      i = 0;
  }
}


Q_CheckValidity(q)
rect_queue *q;
{
  int i;
  i = q->head;
  while (i != q->tail) {
    if (q->r[i].l > 10000 ||
	q->r[i].r > 10000 ||
	q->r[i].t > 10000 ||
	q->r[i].b > 10000 ||
	q->r[i].length > 10000) {
      fprintf(debug_file, "Error in queue[%d]: (%d %d %d %d %d)\n",
	      i, q->r[i].l, q->r[i].r, q->r[i].t, q->r[i].b, q->r[i].length);
    }
    if (++i == q->size)
      i = 0;
  }
  return 0;
}



void Q_Enqueue(q, r)
rect_queue *q;
rect *r;
{

  Q_Checksize(q);
  q->r[q->tail] = *r;

  if (++q->tail == q->size)
    q->tail = 0;


}


void Q_Dequeue(q, r)
rect_queue *q;
rect *r;
{

  *r = q->r[q->head];

  if (++q->head == q->size)
    q->head = 0;
  if (q->randomize && ((q->head == q->randomPt) ||
		       (q->head == q->randomPt + 1))) {
    int i, j, numItems;
    rect temp;
    numItems = (q->tail < q->head)
       ? q->size - q->head + q->tail
       : q->tail - q->head;
    for (i = q->head; i != q->tail; i++) {
      j = (int) (drand48() * numItems) + q->head;
      if (j >= q->size)
	j -= q->size;
      temp = q->r[j];
      q->r[j] = q->r[i];
      q->r[i] = temp;
      if (i == q->size - 1) {
	i = -1;
      }
    }
    q->randomPt = q->tail;
  }
}




int RectBorderLen(r)
rect *r;
{
  return (r->r - r->l) ?
     (r->b - r->t) ?
     (2 * (r->r - r->l + r->b - r->t))
     :
     (r->r - r->l + 1)
     :
     (r->b - r->t) ?
     (r->b - r->t + 1)
     :
     1;
}


PrintHelp(progName)
char *progName;
{
  printf("Options recognized by %s:\n", progName);
  printf("(defaults are in parentheses () )\n");



  printf("   -i <filename>              (none) input file\n");

  printf("   -l <filename>              (\"%s\") name of log file\n", 0);

  printf("   -xpos <xpos>               (%d) window horizontal coordinate\n",
	 -1);
  printf("   -ypos <xpos>               (%d) window vertical coordinate\n",
	 -1);
  printf("   -width <width>             (%d) width of computed area in points\n", 500);
  printf("   -height <height>           (%d) height of computed area in points\n", 500);
  printf("   -boundary <boundary>       (%.1lf) boundary value for M-set computation\n", 2.0);
  printf("   -maxiter <max. iter>       (%d) maximum # of iterations for M-set\n", 1000);
  printf("                              compuptation algorithm\n");
  printf("   -rmin <real min.>          (%.2lf) minimum real coordinate of computed area\n", -2.0);
  printf("   -rmax <real max.>          (%.2lf) maximum real coordinate of computed area\n", 2.0);
  printf("   -imin <imag. min.>         (%.2lf) minimum imaginary coordinate of computed\n", -2.0);
  printf("                              area\n");
  printf("   -imax <imag. max.>         (%.2lf) maximum imaginary coordinate of computed\n", 2.0);
  printf("                              area\n");
  printf("\n");
  printf("      alternate form: (if specified, overrides <r|i><min|max>)\n");
  printf("   -rcenter <real center>     (%.2lf) center real coordinate of computed area\n", (-2.0 + 2.0) / 2);
  printf("   -icenter <imag. center>    (%.2lf) center imaginary coordinate of computed\n", (-2.0 + 2.0) / 2);
  printf("                              area\n");
  printf("   -radius <area radius>      (%.2lf) radius of the computed area\n", (2.0 - -2.0));
  printf("\n");
  printf("   -breakout <breakout size>  (%d) maximum length or width rectangle to\n", 12);
  printf("                              subdivide\n");
  printf("   -colors <# of colors>      (%d) number of colors to request\n", 16);
  printf("   -colreduce <reduce factor> (%d) factor by which to scale down iteration\n", 4);
  printf("                              values to reduce color changes\n");
  printf("   <+,->zoom                  (%s) turn on (off) drag&zoom\n",
	 1 ? "on" : "off");
  printf("   <+,->randomize             (%sset) (on,off) compute regions in as random of\n",
	 1 ? "" : "not ");
  printf("                              order as possible\n");




  printf("   -bw                        (%sset) draw in black and white instead of\n", 0 ? "" : "not ");
  printf("                              color\n");
  exit(0);
}


MPE_Color Iter2Color(flags, iter)
Flags *flags;
int iter;
{
  if (flags->winspecs->bw) {
    return ((iter == flags->maxiter) ? MPE_BLACK :
	    ((iter / flags->colReduceFactor) % 2) ? MPE_WHITE : MPE_BLACK);
  } else {
    if (iter == flags->maxiter) {
      return MPE_BLACK;
    } else {
      return flags->winspecs->colorArray[(iter / flags->colReduceFactor) %
					 flags->winspecs->numColors];
    }
  }
}


ChunkIter2Color(flags, iterData, colorData, size)
Flags *flags;
int *iterData, size;
int *colorData;
{
  int i;

  for (i = 0; i < size; i++) {
    *colorData = Iter2Color(flags, *iterData);

    colorData++;
    iterData++;
  }
}




ComputeChunk(flags, r, pointData, iterData, maxnpoints, npoints)
Flags *flags;
rect *r;
int *iterData, maxnpoints, *npoints;
MPE_Point *pointData;
{
  int i, x, y;

  CalcField(flags->fractal, iterData, r->l, r->r, r->t, r->b);


  *npoints = (r->r - r->l + 1) * (r->b - r->t + 1);
  x = r->l;
  y = r->t;
  for (i = 0; i < *npoints; i++) {
    pointData[i].x = x++;
    pointData[i].y = y;
    pointData[i].c = Iter2Color(flags, iterData[i]);

    if (x > r->r) {
      x = r->l;
      y++;
    }
  }
  return 0;
}



DrawChunk(graph, colorData, r)
MPE_XGraph graph;
int *colorData;
rect r;
{
  int a, b;

  for (b = r.t; b <= r.b; b++) {
    for (a = r.l; a <= r.r; a++) {
      MPE_Draw_point(graph, a, b, *colorData);

      colorData++;
    }
  }
  MPE_Update(graph);
  return 0;
}








































int ComputeBorder(winspecs, flags, rectPtr, pointData, maxnpoints,
		   npoints, isContinuous)
Winspecs *winspecs;
Flags *flags;
rect *rectPtr;
MPE_Point *pointData;
int maxnpoints, *npoints, *isContinuous;
{
  register double re, im, rstep, istep;
  register int x, y;
  register MPE_Point *pointPtr;
  register MPE_Color firstColor;
  rect r;

  r = *rectPtr;



  {
    (re) = (((((((double) ((r.l) - (0)))) * ((((((flags->rmax)) - ((flags->rmin)))) / (((double) ((
		      winspecs->width - 1) - (0)))))))) + ((flags->rmin))));
  };
  {
    (im) = (((((((double) ((r.t) - (0)))) * ((((((flags->imin)) - ((flags->imax)))) / (((double) ((
		     winspecs->height - 1) - (0)))))))) + ((flags->imax))));
  };
  {
    (rstep) = (((((flags->rmax) - (flags->rmin))) / (
					((double) (winspecs->width - 1)))));
  };
  {
    (istep) = (((((flags->imin) - (flags->imax))) / (
				       ((double) (winspecs->height - 1)))));
  };

  pointPtr = pointData + 1;
  pointData->x = r.l;
  pointData->y = r.t;
  pointData->c = firstColor = Iter2Color(flags,
			 (flags->fractal == MBROT) ? MbrotCalcIter(re, im) :
			 (flags->fractal == JULIA) ? JuliaCalcIter(re, im) :
					 MbrotCalcIter(re, im));


  *npoints = r.length;



  switch (flags->fractal) {
  case MBROT:
    if (r.b - r.t > 1 && r.r - r.l > 1) {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {
	n {
	  (re) = (((re) + (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
	if (pointPtr->c != firstColor)
	  goto m1;
	(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto m2;
	(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto m3;
	(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto m4;
	(pointPtr++, y--);
      };;;
      *isContinuous = 1;
      return 1;
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  m1:(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  m2:(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  m3:(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  m4:(pointPtr++, y--);
      };;;
      *isContinuous = 0;
      return 0;
    } else {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
      (pointPtr++, x++);
      };;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
      (pointPtr++, y++);
      };;
      if (r.r - r.l && r.b - r.t) {
	(y = r.b, x = r.r - 1);
	while (x >= r.l) {{
	    (re) = (((re) - (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
	(pointPtr++, x--);
	};;
	(x = r.l, y = r.b - 1);
	while (y > r.t) {{
	    (im) = (((im) - (istep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
	(pointPtr++, y--);
	};;
      } *isContinuous = 0;
      return 0;
    };
  case JULIA:
    if (r.b - r.t > 1 && r.r - r.l > 1) {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {
	n {
	  (re) = (((re) + (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));
	if (pointPtr->c != firstColor)
	  goto j1;
	(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto j2;
	(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto j3;
	(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto j4;
	(pointPtr++, y--);
      };;;
      *isContinuous = 1;
      return 1;
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
  j1:(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
  j2:(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
  j3:(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
  j4:(pointPtr++, y--);
      };;;
      *isContinuous = 0;
      return 0;
    } else {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
      (pointPtr++, x++);
      };;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
      (pointPtr++, y++);
      };;
      if (r.r - r.l && r.b - r.t) {
	(y = r.b, x = r.r - 1);
	while (x >= r.l) {{
	    (re) = (((re) - (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
	(pointPtr++, x--);
	};;
	(x = r.l, y = r.b - 1);
	while (y > r.t) {{
	    (im) = (((im) - (istep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, JuliaCalcIter(re, im));;
	(pointPtr++, y--);
	};;
      } *isContinuous = 0;
      return 0;
    };
  case NEWTON:
    if (r.b - r.t > 1 && r.r - r.l > 1) {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {
	n {
	  (re) = (((re) + (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
	if (pointPtr->c != firstColor)
	  goto n1;
	(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto n2;
	(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto n3;
	(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));
      if (pointPtr->c != firstColor)
	goto n4;
	(pointPtr++, y--);
      };;;
      *isContinuous = 1;
      return 1;
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  n1:(pointPtr++, x++);
      };;;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  n2:(pointPtr++, y++);
      };;;
      (y = r.b, x = r.r - 1);
      while (x >= r.l) {{
	  (re) = (((re) - (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  n3:(pointPtr++, x--);
      };;;
      (x = r.l, y = r.b - 1);
      while (y > r.t) {{
	  (im) = (((im) - (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
  n4:(pointPtr++, y--);
      };;;
      *isContinuous = 0;
      return 0;
    } else {
      (y = r.t, x = r.l + 1);
      while (x <= r.r) {{
	  (re) = (((re) + (rstep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
      (pointPtr++, x++);
      };;
      (x = r.r, y = r.t + 1);
      while (y <= r.b) {{
	  (im) = (((im) + (istep)));
      };
      pointPtr->x = x;
      pointPtr->y = y;
      pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
      (pointPtr++, y++);
      };;
      if (r.r - r.l && r.b - r.t) {
	(y = r.b, x = r.r - 1);
	while (x >= r.l) {{
	    (re) = (((re) - (rstep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
	(pointPtr++, x--);
	};;
	(x = r.l, y = r.b - 1);
	while (y > r.t) {{
	    (im) = (((im) - (istep)));
	};
	pointPtr->x = x;
	pointPtr->y = y;
	pointPtr->c = Iter2Color(flags, MbrotCalcIter(re, im));;
	(pointPtr++, y--);
	};;
      } *isContinuous = 0;
      return 0;
    };
  }
}


DrawBorder(graph, colorData, r)
MPE_XGraph graph;
int *colorData;
rect r;
{
  int x, y;

  for (y = r.t, x = r.l; x <= r.r; x++) {

    MPE_Draw_point(graph, x, y, *colorData);
    colorData++;
  }
  for (x = r.r, y = r.t + 1; y <= r.b; y++) {

    MPE_Draw_point(graph, x, y, *colorData);
    colorData++;
  }
  if (r.r - r.l && r.b - r.t) {
    for (y = r.b, x = r.r - 1; x >= r.l; x--) {

      MPE_Draw_point(graph, x, y, *colorData);
      colorData++;
    }
    for (x = r.l, y = r.b - 1; y > r.t; y--) {

      MPE_Draw_point(graph, x, y, *colorData);
      colorData++;
    }
  }
  MPE_Update(graph);
}


DrawBlock(graph, pointData, r)
MPE_XGraph graph;
MPE_Point *pointData;
rect *r;
{


  MPE_Fill_rectangle(graph, r->l, r->t, r->r - r->l + 1, r->b - r->t + 1,
		     pointData->c);

  MPE_Update(graph);
}
