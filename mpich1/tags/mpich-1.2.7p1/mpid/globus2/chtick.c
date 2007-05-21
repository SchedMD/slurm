#include "chconfig.h"

#if defined(HAVE_MPICHCONF_H) && !defined(MPICHCONF_INC)
/* This includes the definitions found by configure, and can be found in
   the library directory (lib/$ARCH/$COMM) corresponding to this configuration
 */
#define MPICHCONF_INC
#include "mpichconf.h"
#endif


#include "mpidefs.h"


#include "mpid_time.h"
#if defined(HAVE_GETTIMEOFDAY) || defined(USE_WIERDGETTIMEOFDAY) || \
    defined(HAVE_BSDGETTIMEOFDAY)
/* do nothing */
#elif defined(HAVE_CLOCK_GETRES) && defined(HAVE_CLOCK_GETTIME) &&\
      !defined(MPID_CH_Wtime)
#define USING_POSIX_CLOCK
/* FreeBSD incorrectly puts the necessary definitions into sys/time.h 
   (time.h is clearly and explicitly required by Posix).  Since FreeBSD
   didn't implement Posix headers correctly, we do not trust them to 
   implement the Posix clocks correctly.  Instead, use one of the other
   clocks */
#include <time.h>
#endif
/* 
   This returns a value that is correct but not the best value that
   could be returned.
   It makes several separate stabs at computing the tickvalue.
*/
void MPID_CH_Wtick( double *tick )
{
    static double tickval = -1.0;
#if defined(USING_POSIX_CLOCK)
    struct timespec tp;
    if (tickval < 0.0) {
	clock_getres( CLOCK_REALTIME, &tp );
	tickval = (double)(tp.tv_sec) + 1.0e-9 * (double)(tp.tv_nsec);
    }
#else
    double t1, t2;
    int    cnt;
    int    icnt;

    if (tickval < 0.0) {
	tickval = 1.0e6;
	for (icnt=0; icnt<10; icnt++) {
	    cnt = 1000;
	    MPID_Wtime( &t1 );
	    while (cnt--) {
		MPID_Wtime( &t2 );
		if (t2 > t1) break;
		}
	    if (cnt && t2 > t1 && t2 - t1 < tickval)
		tickval = t2 - t1;
	}
    }
#endif
    *tick = tickval;
}
