/*
 * USC.C  (Source file for the Microsecond Clock package)
 *
 * Written by:  Arun Nanda    (07/17/91)
 * Modified by R. Butler
 * High-resolution clock added by Rusty Lusk
 * Tests for timer found at compile time (via TIMER_FOUND and TIMER_USED) 
 * added by Bill Gropp (1/21/00)
 */

#include "usc_sys.h"


VOID usc_init()
{

#if defined(MULTIMAX)

	usc_multimax_timer = timer_init();
	usc_MD_rollover_val = (usc_time_t) ((1<<usc_MD_timer_size)-1);
#define TIMER_FOUND "multimax"

#endif


#if defined(SYMMETRY) || defined(SYMMETRY_PTX)

	unsigned long roll;

	usclk_init();

	roll = 1 << (usc_MD_timer_size-1);
	usc_MD_rollover_val = (usc_time_t) (roll + roll - 1);

#define TIMER_FOUND "symmetry"
#endif


#if defined(TC_2000) || defined(TC_2000_TCMP)

	unsigned long roll;

	roll = 1 << (usc_MD_timer_size-1);
	usc_MD_rollover_val = (usc_time_t) (roll + roll - 1);

#define TIMER_FOUND "tc2000 butterfly"
#endif


#if defined(IPSC860)

	esize_t hwtime;
	unsigned long ustime;

	hwtime.shigh = hwtime.slow = ~0x0;
        hwtime.shigh = (hwtime.shigh & 0x7) << (sizeof(long)*8-3);
        hwtime.slow = ((hwtime.slow >> 3) & ~(0x7 << (sizeof(long)*8-3)))
				| hwtime.shigh;
        ustime = (unsigned long)hwtime.slow * 0.8;
	usc_MD_rollover_val = (usc_time_t) ustime; 

#define TIMER_FOUND "ipsc"
#endif

#if defined(NCUBE)

        unsigned long roll;

        roll = 1 << (usc_MD_timer_size-1);
        usc_MD_rollover_val = (usc_time_t) (roll + roll - 1);

#define TIMER_FOUND "ncube"
#endif


#if defined(FX2800)  ||  defined(FX2800_SWITCH)

    struct hrcval temptime;
    unsigned long roll;

    hrcstamp(&temptime);
    roll = 1 << ((sizeof(usc_time_t) * 8) - 1);
    usc_MD_rollover_val = (usc_time_t) (roll + roll - 1);

#define TIMER_FOUND "Alliant fx2800"
#endif


#if defined(SUN) || defined(HP) || defined(DEC5000) || \
    defined(SUN_SOLARIS) || defined(FREEBSD) || defined(LINUX) || \
    defined(I86_SOLARIS) || defined(NETBSD) || \
    defined(BALANCE) || \
    defined(RS6000)  ||  defined(IBM3090) || \
    defined(NEXT) || defined(TITAN) || defined(GP_1000) || \
    defined(KSR)  || \
    defined(MEIKO_CS2)  || \
    defined(SGI)  || defined(FX8) || defined(CRAY)

	struct timeval tp;
	struct timezone tzp;
	unsigned long roll;

#if defined(SUN_SOLARIS) && defined(USE_WIERDGETTIMEOFDAY)
	gettimeofday(&tp);
#else
	gettimeofday(&tp,&tzp);
#endif
	roll = (usc_time_t) ((usc_time_t) 1 << ((sizeof(usc_time_t)*8)-1));
	roll = roll + roll - 1;
	usc_MD_rollover_val = (usc_time_t) (roll / 1000000);
#define TIMER_FOUND "gettimeofday"
#endif

#ifndef TIMER_FOUND
#errof   "Error - no timer defined.  Please file a bug report"
#endif
}



usc_time_t usc_MD_clock()
{

#if defined(TC_2000) || defined(TC_2000_TCMP)

#define TIMER_USED "tc2000 butterfly"

    struct 
    {
	unsigned long hi;
	unsigned long low;
    } usclock;

    get64bitclock(&usclock);
    return((usc_time_t)usclock.low);

#endif


#if defined(IPSC860)

#define TIMER_USED "ipsc"

	esize_t hwtime;
	unsigned long ustime;

	hwclock(&hwtime);
        hwtime.shigh = (hwtime.shigh & 0x7) << (sizeof(long)*8-3);
        hwtime.slow = ((hwtime.slow >> 3) & ~(0x7 << (sizeof(long)*8-3)))
				| hwtime.shigh;
        ustime = (unsigned long)hwtime.slow * 0.8;
	return( (usc_time_t) ustime);

#endif

#if defined(MEIKO_CS2)

#define TIMER_USED "meiko cs2"
/* making it look like a SUN temporarily (see *.h files also) - RMB */

/****
#include <sys/types.h>
#include <elan/elanreg.h>
#include <elan/elanctxt.h>

    struct elan_timeval t;
    unsigned int uS;

    elan_clock(elan_ctxt,&t);
    uS =  t.tv_sec*1000000 + t.tv_nsec/1000;
    return uS;
*****/
#endif


#if defined(NCUBE)
#define TIMER_USED "ncube"

   unsigned long ustime;
   double amicclk();

    ustime = (unsigned long) amicclk();
    /* printf("usc_MD_clock: returning %lu %u\n",ustime,ustime); */
    return( (usc_time_t) ustime);
#endif


#if defined(FX2800)  ||  defined(FX2800_SWITCH)

#define TIMER_USED "Alliant fx2800"
    struct hrcval temptime;

    hrcstamp(&temptime);
    return ((usc_time_t) (temptime.hv_low * 10));

#endif


#if defined(SUN) || defined(HP) || \
    defined(SUN_SOLARIS) || defined(FREEBSD) || defined(LINUX) || \
    defined(I86_SOLARIS) || defined(NETBSD) || \
    defined(BALANCE) || \
    defined(RS6000) || defined(IBM3090) || \
    defined(NEXT) || defined(TITAN) || defined(TC1000) || \
    defined(KSR)  || \
    defined(MEIKO_CS2)  || \
    defined(SGI) || defined(FX8)

#define TIMER_USED "gettimeofday"

	unsigned long ustime;
	struct timeval tp;
	struct timezone tzp;

#if defined(SUN_SOLARIS) && defined(USE_WIERDGETTIMEOFDAY)
	gettimeofday(&tp);
#else
	gettimeofday(&tp,&tzp);
#endif
	ustime = (unsigned long) tp.tv_sec;
	ustime = ustime % usc_MD_rollover_val;
	ustime = (ustime * 1000000) + (unsigned long) tp.tv_usec;

	return((usc_time_t) ustime);

#endif

#if defined(DEC5000)        
       
/*        
 *  hack by pak@regent.e-technik.tu-muenchen.de:
 *
 *    clock resolution is 3906 us        
 *    we can do about 120 calls in 3906 us == 32 us per call        
 */        

#define TIMER_USED "dec5000"

        unsigned long ustime;        
        struct timeval tp;        
        struct timezone tzp;        
        static unsigned long last= 0;        
        static unsigned long increment= 0;        
       
        gettimeofday(&tp,&tzp);        
        ustime = (unsigned long) tp.tv_sec;        
        ustime = ustime % usc_MD_rollover_val;        
        ustime = (ustime * 1000000) + (unsigned long) tp.tv_usec;        
       
        if (last == ustime)
        {
          increment += 33L;
        }
        else
        {
          last = ustime;
          increment = 0;
        }

        return((usc_time_t) last + increment);

#endif

#ifndef TIMER_USED
    'Error - no timer code used.  Please file a bug report'
#endif
}
