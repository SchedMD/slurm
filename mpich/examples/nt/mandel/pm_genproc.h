

#ifndef _PM_GENPROC_H_
#define _PM_GENPROC_H_

#define IS_Q_EMPTY(q) (q.head == q.tail)

#define IS_SET(z) ( \
  (bw==MAXITER_SHADE) ? ((z)==maxiter) : \
  (bw==EVEN_SHADE)    ? ((z)==maxiter || !((z)%2)) : (z))

#define ITER2COLOR( iter ) (  \
  (bw) ? ((iter)==maxiter || !((iter)%2)) : \
  ((iter)==maxiter) ? (MPE_BLACK) : ((iter) % (numColors-1) + 1) )

#define RECT_ASSIGN( rect, w, x, y, z ) { \
  (rect).l = (w); (rect).r = (x); (rect).t = (y); (rect).b = (z); }

#ifdef __STDC__
typedef int Fract_FN(NUM,NUM);
#else
typedef int Fract_FN();
#endif

void PrintHelp( char *progName );
int DefineMPITypes();
int GetDefaultWinspecs( Winspecs *winspecs );
int GetDefaultFlags( Winspecs *winspecs, Flags *flags );
int GetWinspecs( int *argc, char **argv, Winspecs *winspecs );
int GetFlags( int *argc, char **argv, Winspecs *winspecs, Flags *flags );
int StrContainsNonWhiteSpace( char *str );
int Pixel2Complex( Flags *flags, int x, int y, NUM *nx, NUM *ny );
void Q_Create( rect_queue *q, int randomize );
int RectBorderLen( rect *r );
void Q_Enqueue( rect_queue *q, rect *r );
void Q_Dequeue( rect_queue *q, rect *r );
int ComputeChunk( Flags *flags, rect *r, MPE_Point *pointData, int *iterData, int maxnpoints, int *npoints );
int ComputeBorder( Winspecs *winspecs, Flags *flags, rect *rectPtr, MPE_Point *pointData, int maxnpoints, int *npoints, int *isContinuous );
void DrawBlock( MPE_XGraph graph, MPE_Point *pointData, rect *r );

#endif

/*  _PM_GENPROC_H_ */
