/* 
   This is sample code originally written for the Encore Multimax.
   The original may be found in ~gropp/fmmp/barrier.c 

   The intent is to make this available on general systems that 
   have shared memory or global address spaces.

  (note that one the Cray T3D, since the shmem_put and shmem_get operations
  are NOT cache-coherent, these may not work well on that system)
 */
/* change to the new (libpp) names */

#include "p4.h"

#define NULL  0
#define NPMAX 20


struct fastbar {
  int np, mypid;
  int    *phase, *myphase, *p1,*p2,*p3,*p4,*p5;
  double *value, *myvalue, *v1,*v2,*v3,*v4,*v5;
};

/*
    initbar returns a pointer to a fastbar structure.
    flag says whether this process should do acquire the shared memory and
    broadcast it or not.  flag = 1 -> get the memory
*/

void *initbar(npf,flag)
int *npf, flag;
{
    int i;
    struct fastbar *bar;

    bar   = (struct fastbar *) p4_malloc(sizeof (struct fastbar));
    bar->np = *npf;
    if (flag)
    {
	bar->phase = (int *)    p4_shmalloc(NPMAX*sizeof(int));
	bar->value = (double *) p4_shmalloc(NPMAX*sizeof(double));
	for (i = 0;  i < bar->np;  i++)
	  bar->phase[i] = 0;
    }
    return (void *) bar;
}

pidbar(bar,mypidf)
struct fastbar *bar;
int *mypidf;
{
    int mypid,np;
    int *phase;
    double *value;

    mypid = bar->mypid = *mypidf;
    np    = bar->np;
    phase = bar->phase;
    value = bar->value;

    bar->myphase = &phase[mypid];
    bar->p1 = (mypid%2 == 0  &&  mypid+1 < np) ?  &phase[mypid+1] : NULL;
    bar->p2 = (mypid%4 == 0  &&  mypid+2 < np) ?  &phase[mypid+2] : NULL;
    bar->p3 = (mypid%8 == 0  &&  mypid+4 < np) ?  &phase[mypid+4] : NULL;
    bar->p4 = (mypid%16== 0  &&  mypid+8 < np) ?  &phase[mypid+8] : NULL;
    bar->p5 = (mypid%32== 0  &&  mypid+16< np) ?  &phase[mypid+16]: NULL;
    bar->myvalue = &value[mypid];
    bar->v1 = (mypid%2 == 0  &&  mypid+1 < np) ?  &value[mypid+1] : NULL;
    bar->v2 = (mypid%4 == 0  &&  mypid+2 < np) ?  &value[mypid+2] : NULL;
    bar->v3 = (mypid%8 == 0  &&  mypid+4 < np) ?  &value[mypid+4] : NULL;
    bar->v4 = (mypid%16== 0  &&  mypid+8 < np) ?  &value[mypid+8] : NULL;
    bar->v5 = (mypid%32== 0  &&  mypid+16< np) ?  &value[mypid+16]: NULL;
}

waitbar(bar)
struct fastbar *bar;
{
    register int oldphase;
    
    oldphase = *(bar->myphase);
    if (bar->p1)  {while (*(bar->p1) == oldphase) ;
    if (bar->p2)  {while (*(bar->p2) == oldphase) ;
    if (bar->p3)  {while (*(bar->p3) == oldphase) ;
    if (bar->p4)  {while (*(bar->p4) == oldphase) ;
    if (bar->p5)  {while (*(bar->p5) == oldphase) ; }}}}}
    ++(*(bar->myphase));
    while (*(bar->phase) == oldphase) ;
}

double sumbar(bar,x)
struct fastbar *bar;
double *x;
{
    register int oldphase;
    register double sum;

    oldphase = *(bar->myphase);         sum = *x;
    if (bar->p1)  {while (*(bar->p1) == oldphase) ; sum += *(bar->v1);
    if (bar->p2)  {while (*(bar->p2) == oldphase) ; sum += *(bar->v2);
    if (bar->p3)  {while (*(bar->p3) == oldphase) ; sum += *(bar->v3);
    if (bar->p4)  {while (*(bar->p4) == oldphase) ; sum += *(bar->v4);
    if (bar->p5)  {while (*(bar->p5) == oldphase) ; sum += *(bar->v5); }}}}}
    *(bar->myvalue) = sum;  ++(*(bar->myphase));
    while (*(bar->phase) == oldphase) ;
    return (*(bar->value));
}

double maxbar(bar,x)
struct fastbar *bar;
double *x;
{
    register int oldphase;
    register double max;

    oldphase = *(bar->myphase);                max = *x;
    if (bar->p1)  {while (*(bar->p1) == oldphase) ; if (max < *(bar->v1)) max = *(bar->v1);
    if (bar->p2)  {while (*(bar->p2) == oldphase) ; if (max < *(bar->v2)) max = *(bar->v2);
    if (bar->p3)  {while (*(bar->p3) == oldphase) ; if (max < *(bar->v3)) max = *(bar->v3);
    if (bar->p4)  {while (*(bar->p4) == oldphase) ; if (max < *(bar->v4)) max = *(bar->v4);
    if (bar->p5)  {while (*(bar->p5) == oldphase) ; if (max < *(bar->v5)) max = *(bar->v5); }}}}}
    *(bar->myvalue) = max;  ++(*(bar->myphase));
    while (*(bar->phase) == oldphase) ;
    return (*(bar->value));
}

double minbar(bar,x)
struct fastbar *bar;
double *x;
{
    register int oldphase;
    register double min;

    oldphase = *(bar->myphase);                min = *x;
    if (bar->p1)  {while (*(bar->p1) == oldphase) ; if (min > *(bar->v1)) min = *(bar->v1);
    if (bar->p2)  {while (*(bar->p2) == oldphase) ; if (min > *(bar->v2)) min = *(bar->v2);
    if (bar->p3)  {while (*(bar->p3) == oldphase) ; if (min > *(bar->v3)) min = *(bar->v3);
    if (bar->p4)  {while (*(bar->p4) == oldphase) ; if (min > *(bar->v4)) min = *(bar->v4);
    if (bar->p5)  {while (*(bar->p5) == oldphase) ; if (min > *(bar->v5)) min = *(bar->v5); }}}}}
    *(bar->myvalue) = min;  ++(*(bar->myphase));
    while (*(bar->phase) == oldphase) ;
    return (*(bar->value));
}
