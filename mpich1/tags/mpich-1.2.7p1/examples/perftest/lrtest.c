#ifndef lint
static char vcid[] = "$Id: lrtest.c,v 1.2 1998/04/29 15:15:43 swider Exp $";
#endif

/*
    This file contains a routine to perform a timing test and collect
    data for a linear-regression estimate for a model (s+r n), where
    n is the parameter
 */

#include "tools.h"
#include "testing/lrctx.h"       /*I "testing/lrctx.h" I*/

void LRComputeRate();

LRctx *LRCreate()
{
LRctx *new;

new              = NEW(LRctx);     CHKPTRN(new);
new->sumlen      = 0.0;
new->sumtime     = 0.0;
new->sumlen2     = 0.0;
new->sumlentime  = 0.0;
new->sumtime2    = 0.0;
new->ntest       = 0.0;
new->minreps     = 3;
new->maxreps     = 30;
new->NatThresh   = 3;
new->repsThresh  = 0.05;

return new;
}
/*
  This runs the tests for a single size.  It adapts to the number of 
  tests necessary to get a reliable value for the minimum time.
  It also keeps track of the average and maximum times (which are unused
  for now).

  We can estimate the variance of the trials by using the following 
  formulas:

  variance = (1/N) sum (t(i) - (s+r n(i))**2
           = (1/N) sum (t(i)**2 - 2 t(i)(s + r n(i)) + (s+r n(i))**2)
	   = (1/N) (sum t(i)**2 - 2 s sum t(i) - 2 r sum t(i)n(i) + 
	      sum (s**2 + 2 r s n(i) + r**2 n(i)**2))
  Since we compute the parameters s and r, we need only maintain
              sum t(i)**2
              sum t(i)n(i)
              sum n(i)**2
  We already keep all of these in computing the (s,r) parameters; this is
  simply a different computation.

  In the case n == constant (that is, inside a single test), we can use
  a similar test to estimate the variance of the individual measurements.
  In this case, 

  variance = (1/N) sum (t(i) - s**2
           = (1/N) sum (t(i)**2 - 2 t(i)s + s**2)
	   = (1/N) (sum t(i)**2 - 2 s sum t(i) + sum s**2)
  Here, s = sum t(i)/N
  (For purists, the divison should be slightly different from (1/N) in the
  variance formula.  I'll deal with that later.)

 */

/*@
    LRRunSingleTest - Run a single test of f for time

    Input Parameters:
+   lrctx - Context
.   f     - function to run test.  Returns time test took
.   fctx  - Context to pass to test function
-   x     - parameter to pass to function (also parameter in model)

$    Format of function is
$       time = f( x, fctx )
@*/ 
double LRRunSingleTest( lrctx, f, fctx, x )
LRctx  *lrctx;
double (*f)();
void   *fctx;
double x;
{
int    k, natmin;
double t, tmin, tmax, tsum;


tmin   = 1.0e+38;
tmax   = tsum = 0.0;
natmin = 0;

for (k=0; k<lrctx->maxreps; k++) {
    t = (* f) (x,fctx);
    tsum += t;
    if (t > tmax) tmax = t;
    if (t < tmin) {
        tmin   = t;
        natmin = 0;
        }
    else if (lrctx->minreps < k &&
             tmin * (1.0 + lrctx->repsThresh) > t) {
        /* This time is close to the minimum; use this to decide
           that we've gotten close enough */
        natmin++;
        if (natmin >= lrctx->NatThresh)
	    break;
	}
    }

lrctx->sumlen     += x;
lrctx->sumtime    += tmin;
lrctx->sumlen2    += x * x;
lrctx->sumlentime += tmin * x;
lrctx->sumtime2   += tmin * tmin;
lrctx->ntest      ++;

return tmin;
}

void LRComputeParams( lrctx, s, r )
LRctx  *lrctx;
double *s, *r;
{
LRComputeRate( lrctx->sumlen, lrctx->sumtime, lrctx->sumlentime,
	       lrctx->sumlen2, lrctx->ntest, s, r );
}	

void LRDestroy( lrctx )
LRctx *lrctx;
{
FREE( lrctx );
}

/*
Change to RunSingleTest-
Individual routines do

PIgscat( &t, issrc == PImytid == proc1 )

*/

void LRComputeRate( sumlen, sumtime, sumlentime, sumlen2, ntest, s, r )
int    ntest;
double sumlen, sumlen2, sumtime, sumlentime;
double *s, *r;
{
double R, S;

R = sumlen * sumlen - ntest * sumlen2;
if (R == 0.0) {
    *s = *r = 0.0;
    return;
    }
R = (sumlen * sumtime - ntest * sumlentime) / R;
S = (sumtime - R * sumlen) / ntest;

if (S < 0 || R < 0) {
    S = 0.0;
    R = sumlentime / sumlen2;
    }
*r = R;
*s = S;
}
