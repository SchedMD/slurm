#include "mpeconf.h"
/* 
   This file contains simple binary read/write routines.
 */
#include <errno.h>
#if defined(HAVE_UNISTD_H)
#include <unistd.h>
#endif
#include "clogimpl.h"

#if !(defined(WORDS_BIGENDIAN))
/*
  CLOGByteSwapInt - Swap bytes in an integer
*/
void CLOGByteSwapInt(int *buff,int n)
{
  int  i,j,tmp =0;
  int  *tptr = &tmp;          /* Need to access tmp indirectly to get */
                                /* arround the bug in DEC-ALPHA compilers*/
  char *ptr1,*ptr2 = (char *) &tmp;

  for ( j=0; j<n; j++ ) {
    ptr1 = (char *) (buff + j);
    for (i=0; i<sizeof(int); i++) {
      ptr2[i] = ptr1[sizeof(int)-1-i];
    }
    buff[j] = *tptr;
  }
}
/* --------------------------------------------------------- */
/*
  CLOGByteSwapDouble - Swap bytes in a double
*/
void CLOGByteSwapDouble(double *buff,int n)
{
  int    i,j;
  double tmp,*buff1 = (double *) buff;
  double *tptr = &tmp;          /* take care pf bug in DEC-ALPHA g++ */
  char   *ptr1,*ptr2 = (char *) &tmp;

  for ( j=0; j<n; j++ ) {
    ptr1 = (char *) (buff1 + j);
    for (i=0; i<sizeof(double); i++) {
      ptr2[i] = ptr1[sizeof(double)-1-i];
    }
    buff1[j] = *tptr;
  }
}
#else
void CLOGByteSwapDouble (double *buff, int n){}
void CLOGByteSwapInt (int *buff, int n) {}
#endif
