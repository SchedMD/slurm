

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

#endif

/*  _PM_GENPROC_H_ */
