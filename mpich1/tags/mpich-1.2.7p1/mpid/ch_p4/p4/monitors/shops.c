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
static int np, mypid;
static int    *phase, *myphase, *p1,*p2,*p3,*p4,*p5;
static double *value, *myvalue, *v1,*v2,*v3,*v4,*v5;

initbar(npf)
int *npf;
{
    int i;

    np = *npf;
    phase = (int *)    p4_shmalloc(NPMAX*sizeof(int));
    value = (double *) p4_shmalloc(NPMAX*sizeof(double));
    for (i = 0;  i < np;  i++)
	phase[i] = 0;
}

pidbar(mypidf)
int *mypidf;
{
    mypid = *mypidf - 1;
    myphase = &phase[mypid];
    p1 = (mypid%2 == 0  &&  mypid+1 < np) ?  &phase[mypid+1] : NULL;
    p2 = (mypid%4 == 0  &&  mypid+2 < np) ?  &phase[mypid+2] : NULL;
    p3 = (mypid%8 == 0  &&  mypid+4 < np) ?  &phase[mypid+4] : NULL;
    p4 = (mypid%16== 0  &&  mypid+8 < np) ?  &phase[mypid+8] : NULL;
    p5 = (mypid%32== 0  &&  mypid+16< np) ?  &phase[mypid+16]: NULL;
    myvalue = &value[mypid];
    v1 = (mypid%2 == 0  &&  mypid+1 < np) ?  &value[mypid+1] : NULL;
    v2 = (mypid%4 == 0  &&  mypid+2 < np) ?  &value[mypid+2] : NULL;
    v3 = (mypid%8 == 0  &&  mypid+4 < np) ?  &value[mypid+4] : NULL;
    v4 = (mypid%16== 0  &&  mypid+8 < np) ?  &value[mypid+8] : NULL;
    v5 = (mypid%32== 0  &&  mypid+16< np) ?  &value[mypid+16]: NULL;
}

waitbar()
{
    register int oldphase;

    oldphase = *myphase;
    if (p1)  {while (*p1 == oldphase) ;
    if (p2)  {while (*p2 == oldphase) ;
    if (p3)  {while (*p3 == oldphase) ;
    if (p4)  {while (*p4 == oldphase) ;
    if (p5)  {while (*p5 == oldphase) ; }}}}}
    ++*myphase;
    while (*phase == oldphase) ;
}

double sumbar(x)
double *x;
{
    register int oldphase;
    register double sum;

    oldphase = *myphase;                sum = *x;
    if (p1)  {while (*p1 == oldphase) ; sum += *v1;
    if (p2)  {while (*p2 == oldphase) ; sum += *v2;
    if (p3)  {while (*p3 == oldphase) ; sum += *v3;
    if (p4)  {while (*p4 == oldphase) ; sum += *v4;
    if (p5)  {while (*p5 == oldphase) ; sum += *v5; }}}}}
    *myvalue = sum;  ++*myphase;
    while (*phase == oldphase) ;
    return (*value);
}

double maxbar(x)
double *x;
{
    register int oldphase;
    register double max;

    oldphase = *myphase;                max = *x;
    if (p1)  {while (*p1 == oldphase) ; if (max < *v1) max = *v1;
    if (p2)  {while (*p2 == oldphase) ; if (max < *v2) max = *v2;
    if (p3)  {while (*p3 == oldphase) ; if (max < *v3) max = *v3;
    if (p4)  {while (*p4 == oldphase) ; if (max < *v4) max = *v4;
    if (p5)  {while (*p5 == oldphase) ; if (max < *v5) max = *v5; }}}}}
    *myvalue = max;  ++*myphase;
    while (*phase == oldphase) ;
    return (*value);
}

double minbar(x)
double *x;
{
    register int oldphase;
    register double min;

    oldphase = *myphase;                min = *x;
    if (p1)  {while (*p1 == oldphase) ; if (min > *v1) min = *v1;
    if (p2)  {while (*p2 == oldphase) ; if (min > *v2) min = *v2;
    if (p3)  {while (*p3 == oldphase) ; if (min > *v3) min = *v3;
    if (p4)  {while (*p4 == oldphase) ; if (min > *v4) min = *v4;
    if (p5)  {while (*p5 == oldphase) ; if (min > *v5) min = *v5; }}}}}
    *myvalue = min;  ++*myphase;
    while (*phase == oldphase) ;
    return (*value);
}
