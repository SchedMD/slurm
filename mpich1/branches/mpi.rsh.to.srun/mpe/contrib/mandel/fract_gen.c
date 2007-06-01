/* general routines for calculating fractals */
#include <stdio.h>
#if HAVE_STDLIB_H
#include <stdlib.h>
#endif
#include <math.h>
#include "pmandel.h"
#include "fract_gen.h"

#ifndef DEBUG
#define DEBUG 0
#endif

static NUM rmin, rmax, imin, imax;
static int xmin, xmax, ymin, ymax;

static int mbrotrep_maxiter=200, mbrotrep_miniter=100;
static int mbrotrep_longestCycle=10;
static double mbrotrep_boundary_sq=16, mbrotrep_fudgeFactor=.001;
typedef NUM two_NUM[2];
static two_NUM *mbrotrep_lastMoves;


static Mbrot_settings mbrot_settings;
static Julia_settings julia_settings;
static Newton_settings newton_settings;


void Fract_SetRegion( newrmin, newrmax, newimin, newimax,
		      newxmin, newxmax, newymin, newymax )
NUM newrmin, newrmax, newimin, newimax;
int newxmin, newxmax, newymin, newymax;
{
  rmin = newrmin;
  rmax = newrmax;
  imin = newimin;
  imax = newimax;
  xmin = newxmin;
  xmax = newxmax;
  ymin = newymin;
  ymax = newymax;
}



void Mbrot_Settings( boundary_sq, maxiter )
double boundary_sq;
int maxiter;
{
  mbrot_settings.boundary_sq = boundary_sq;
  mbrot_settings.maxiter = maxiter;
}


void Newton_Settings( epsilon, coeff, nterms )
double epsilon;
int *coeff;
int nterms;
{
  newton_settings.epsilon = epsilon;
  newton_settings.coeff = coeff;
  newton_settings.nterms = nterms;
}


void Julia_Settings(boundary_sq, maxiter, real, imag)
double boundary_sq;
int maxiter;
NUM real, imag;
{
  julia_settings.boundary_sq = boundary_sq;
  julia_settings.maxiter = maxiter;
  NUM_ASSIGN(julia_settings.r, real);
  NUM_ASSIGN(julia_settings.i, imag);
}

void Mbrotrep_Settings(boundary, maxiter, miniter, longestCycle, fudgeFactor)
/* When performing the Mandelbrot transformation on points that are in the
   set, eventually the sequence of numbers will reach fall into a repetative
   cycle.  Mbrotrep plots the length of these cycles.  Points near the center
   of the set should have very short cycles--1 or 2 iterations long.  Points
   on the fringes of the set will have longer cycles, and points outside the
   set will not have cycles.  Their cycle length will be denoted as 0.

   boundary - if the sequence exceeds 'boundary', it is assumed not to be in
              the set, and its cycle length is set to 0
   maxiter - maximum number of iterations to compute, looking for a cycle
   longestCycle - maximum length of cycle to look for; should be <= maxiter
   fudgeFactor - it will be faster and more accurate (I think) to give
                  some leeway in considering two elements in the sequence
		  identical.  For example, {1.1235, .03452, 1.1231, .03456,...}
		  is close enough to a cycle length of 2, if we set the
		  fudgeFactor to <.0004.  As we zoom into a narrower range of
		  points in the complex plane, we should scale down the
		  fudgeFactor accordingly.
*/
double boundary, fudgeFactor;
int maxiter, miniter, longestCycle;
{
  mbrotrep_boundary_sq = boundary*boundary;
  mbrotrep_maxiter = maxiter;
  mbrotrep_miniter = miniter;
  mbrotrep_longestCycle = longestCycle;
  mbrotrep_fudgeFactor = fudgeFactor;
  mbrotrep_lastMoves = (two_NUM *)malloc(sizeof(NUM[2])*longestCycle);
}


MbrotCalcIter (re, im)
NUM re, im;
{
  register NUM zr, zi, temp;
  register int k;

  /* set initial value - Z[0] = c */
  k=0; zr=re; zi=im;

#if DEBUG>2
  fprintf( debug_file, "maxiter = %d, boundary = %lf\n",
	   mbrot_settings.maxiter, mbrot_settings.boundary_sq );
#endif

  while (k<mbrot_settings.maxiter &&
	 COMPLEX_MAGNITUDE_SQ(zr,zi)<mbrot_settings.boundary_sq) {
#if DEBUG>2
    fprintf( debug_file, "zr: %10lf zi: %10lf\n", zr, zi );
#endif
    COMPLEX_SQUARE(zr,zi,temp);
    COMPLEX_ADD(zr,zi,re,im);
    k++;
  }

#if DEBUG>1
  fprintf( debug_file, "mbrot calc: at (%lf %lf) %d\n", re, im, k );
#endif

  
  return k;
}


JuliaCalcIter (re, im)
NUM re, im;
{
  register NUM zr, zi, temp, jr, ji;
  register int k;

  /* set initial value - Z[0] = c */
  k=0; zr=re; zi=im;
  NUM_ASSIGN( jr, julia_settings.r );
  NUM_ASSIGN( ji, julia_settings.i );

  while (k<julia_settings.maxiter &&
	 COMPLEX_MAGNITUDE_SQ(zr,zi)<julia_settings.boundary_sq) {
    COMPLEX_SQUARE(zr,zi,temp);
    COMPLEX_ADD(zr,zi,jr,ji);
    k++;
  }
  
  return k;
}


MbrotrepCalcIter (re, im)
NUM re, im;
{
  register NUM zr, zi, temp;
  register int k, lmi, j;
  int len;
  /* lmi - lastMove index */

#ifdef DEBUG
printf ("\n<%lf %lf>: ", re, im);
#endif

  for (lmi=0; lmi<mbrotrep_longestCycle; lmi++) {
    mbrotrep_lastMoves[lmi][0] = mbrotrep_lastMoves[lmi][1] = 0;
  }

  /* set initial value - Z[0] = c */
  k=lmi=0; zr=re; zi=im;

  while (k<mbrotrep_maxiter &&
	 COMPLEX_MAGNITUDE_SQ(zr,zi)<mbrotrep_boundary_sq) {
    COMPLEX_SQUARE(zr,zi,temp);
    COMPLEX_ADD(zr,zi,re,im);
    k++;
    if (k>mbrotrep_miniter) {
      j=lmi;
#ifdef DEBUG
printf ("(%lf %lf) ", zr, zi);
#endif
      do {
	j--;
	if (j<0) j=mbrotrep_longestCycle-1;
	if (fabs(mbrotrep_lastMoves[j][0]-zr)<mbrotrep_fudgeFactor &&
	    fabs(mbrotrep_lastMoves[j][1]-zi)<mbrotrep_fudgeFactor) {
	  len = lmi-j;
	  if (len<1) len+=mbrotrep_longestCycle;
#ifdef DEBUG
printf (" loop:  %lf - %lf=%lf < %lf (%d), %lf - %lf=%lf < %lf (%d)\n\n",
	mbrotrep_lastMoves[j][0], zr, fabs(mbrotrep_lastMoves[j][0]-zr),
	mbrotrep_fudgeFactor,
	fabs(mbrotrep_lastMoves[j][0]-zr) < mbrotrep_fudgeFactor,
	mbrotrep_lastMoves[j][1], zi, fabs(mbrotrep_lastMoves[j][1]-zi),
	mbrotrep_fudgeFactor,
	fabs(mbrotrep_lastMoves[j][1]-zi) < mbrotrep_fudgeFactor);
#endif
	  return len;
	}
      } while (j!=lmi);
    }
    mbrotrep_lastMoves[lmi][0] = zr;

    mbrotrep_lastMoves[lmi][1] = zi;
    lmi++; if (lmi==mbrotrep_longestCycle) lmi=0;
  }
#ifdef DEBUG
  printf ("Bought it.  %d %d\n",   k<mbrotrep_maxiter,
	 COMPLEX_MAGNITUDE_SQ(zr,zi)<mbrotrep_boundary_sq);
#endif
  return 0;
}


void CalcField (fn, fieldVal,
		xstart, xend, ystart, yend)
int *fieldVal;
int xstart, xend, ystart, yend;
Fractal_type fn;
{
  int height, width, i, j;
  register NUM rstep, istep, real, imag, rstart, rend, istart, iend;

  height = yend-ystart+1;
  width = xend-xstart+1;

     /* get the bounding coordinates in the complex plane */
  NUM_ASSIGN( rstart, COORD2CMPLX( rmin, rmax, xmin, xmax, xstart));
  NUM_ASSIGN( real, rstart);
  NUM_ASSIGN( rend,   COORD2CMPLX( rmin, rmax, xmin, xmax, xend));
  NUM_ASSIGN( istart, COORD2CMPLX( imax, imin, ymin, ymax, ystart));
  NUM_ASSIGN( iend,   COORD2CMPLX( imax, imin, ymin, ymax, yend));
  NUM_ASSIGN( imag, istart );
  NUM_ASSIGN( rstep,  NUM_DIV( NUM_SUB( rend, rstart ),
			       INT2NUM( width-1 ) ) );
  NUM_ASSIGN( istep,  NUM_DIV( NUM_SUB( iend, istart ),
			       INT2NUM( height-1 ) ) );

  switch (fn) {
  case MBROT:
    for (j=0; j<height; j++) {
      for (i=0; i<width; i++) {
	*fieldVal = MbrotCalcIter(real, imag);
#if DEBUG>1
	fprintf ( debug_file, "ComputeField: (%3d,%3d): %d\n",
		  i+xstart, j+ystart, *fieldVal );
#endif
	fieldVal++;
	NUM_ASSIGN( real, NUM_ADD( real, rstep ) );
      }
      NUM_ASSIGN( real, rstart );
      NUM_ASSIGN( imag, NUM_ADD( imag, istep ) );
    }
    break;

  case JULIA:
    for (j=0; j<height; j++) {
      for (i=0; i<width; i++) {
	fieldVal[j*width+i] = JuliaCalcIter(real, imag);
#if DEBUG>1
	fprintf ( debug_file, "(%3d,%3d): %10lf %10lf: %d\n", i, j, real,
		  imag, fieldVal[j*width+i]);
#endif
	NUM_ASSIGN( real, NUM_ADD( real, rstep ) );
      }
      NUM_ASSIGN( real, rstart );
      NUM_ASSIGN( imag, NUM_ADD( imag, istep ) );
    }
    break;

  case NEWTON:
    /* NOTE:  Visualization of Newton's approximmation is not implemented
       yet.  Sorry for the inconvenience */
    for (j=0; j<height; j++) {
      for (i=0; i<width; i++) {
	fieldVal[j*width+i] = MbrotCalcIter(real, imag);
#if DEBUG>1
	fprintf ( debug_file, "(%3d,%3d): %10lf %10lf: %d\n", i, j, real,
		  imag, fieldVal[j*width+i]);
#endif
	NUM_ASSIGN( real, NUM_ADD( real, rstep ) );
      }
      NUM_ASSIGN( real, rstart );
      NUM_ASSIGN( imag, NUM_ADD( imag, istep ) );
    }
    break;
  }  /* end switch */
}


void CopySub2DArray (mainArray, subArray, mainWidth, mainHeight,
		    subWidth, subHeight, xpos, ypos)
int *mainArray, *subArray, mainWidth, mainHeight, subWidth, subHeight,
    xpos, ypos;

/* CopySub2DArray copies one 2d array of ints  stored as a 1d array into another 2d array
   of ints stored as a 1d array.  For example:

   mainArray = 10x10 array, all 0's
   subArray = 5x3 array, all 1's
   copy subArray to 2,3 within mainArray

   CopySub2DArray( mainArray, subArray, 10, 10, 5, 3, 2, 3 );

   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0     1 1 1 1 1     0 0 1 1 1 1 1 0 0 0
   0 0 0 0 0 0 0 0 0 0  +  1 1 1 1 1  =  0 0 1 1 1 1 1 0 0 0
   0 0 0 0 0 0 0 0 0 0     1 1 1 1 1     0 0 1 1 1 1 1 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0
   0 0 0 0 0 0 0 0 0 0                   0 0 0 0 0 0 0 0 0 0

   If the copy goes outside the bounds of the mainArray, none of the copy is performed
*/
{
  int i, j, *ptr, *fromPtr, *toPtr;

  if (mainWidth<subWidth+xpos || mainHeight<subHeight+ypos) {   /* make sure we don't overrun */
    return;
  }

  fromPtr = subArray;			       /* set read location at upper left corner of subArray */
  toPtr = mainArray+ypos*mainWidth+xpos;       /* set write location at upper left corner in mainArray */

  for (j=0; j<subHeight; j++) {
    for (i=0; i<subWidth; i++) {
      *toPtr++ = *fromPtr++;
    }
    toPtr += mainWidth-subWidth;
  }
}
