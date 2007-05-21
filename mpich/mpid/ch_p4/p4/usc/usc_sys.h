/*
 * USC_MAIN.H  (Private header file for the Microsecond Clock package)
 *
 * Written by:  Arun Nanda    (07/17/91)
 * Modified by R. Butler
 *
 * The following machine-specific libraries need to be linked in
 * with the application using the UST functions:
 *
 */


#include "usc.h"


#if defined(MULTIMAX)

#    include <parallel.h>
#    define usc_MD_timer_size  (sizeof(unsigned)*8)
     unsigned *usc_multimax_timer;

#endif


#if defined(SYMMETRY) || defined(SYMMETRY_PTX)

#    define usc_MD_timer_size  (sizeof(usclk_t)*8)

#endif


#if defined(TC_2000) || defined(TC_2000_TCMP)

#    define usc_MD_timer_size  (sizeof(unsigned long)*8)

#endif

#if defined(MEIKO_CS2)
#    define usc_MD_timer_size  (sizeof(unsigned)*8)
#endif

#if defined(IPSC860)

#    if defined (DELTA)
#        include <mesh.h>
#    else
#        if defined(PARAGON)
#            include <nx.h>
#        else
#            include <cube.h>
#        endif
#    endif
#    define usc_MD_timer_size ((sizeof(long)*8)+3)
#    define usc_MD_ticks_per_usec (HWHZ/1000000)

#endif

#if defined(NCUBE)

#    define usc_MD_timer_size  (sizeof(unsigned long)*8)

#endif


#if defined(FX2800)  ||  defined(FX2800_SWITCH)
#       include <sys/time.h>
#endif

#if defined(SUN) || defined(DEC5000) || defined(HP) \
    || defined(SUN_SOLARIS) || defined(FREEBSD) || defined(LINUX) \
    || defined(I86_SOLARIS) || defined(NETBSD) \
    || defined(BALANCE) \
    || defined(IBM3090) || defined(RS6000) \
    || defined(NEXT) || defined(TITAN) || defined(GP_1000) \
    || defined(KSR) \
    || defined(MEIKO_CS2) \
    || defined(SGI) || defined(FX8) || defined(CRAY)

#	include <sys/time.h>

#endif


usc_time_t usc_MD_rollover_val = 0;

