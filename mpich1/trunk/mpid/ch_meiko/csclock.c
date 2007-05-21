/* 
   This is Meiko-specific code to provide access to the high-resolution
   clock on the Meiko CS2
 */
#include <sys/types.h>
/* The documentation says to use elan/elan.h; this is now in 
   /opt/MEIKOcs2/include */
#include <elan/elan.h>


/****************************************************************
Function GET_NSEC_CLOCK() - Gets the system time from the
nanosecond clock.
****************************************************************/

double MPID_get_nsec_clock()
{
    ELAN_TIMEVAL clock;
    int          secs;
    int          nsecs;
    static int   init_nsec_clock;
    static void  *context;
    
    if (init_nsec_clock == 0)
    {
	context         = elan_init();
	init_nsec_clock = 1;
    }
    
    elan_clock(context, (ELAN_TIMEVAL *)&clock);
    
    secs  = clock.tv_sec;
    nsecs = clock.tv_nsec;
    
    return((double)secs + (double)nsecs * (double)0.000000001);
    
}   /* end GET_NSEC_CLOCK */

