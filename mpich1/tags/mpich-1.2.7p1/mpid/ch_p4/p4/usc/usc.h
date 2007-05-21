/*
 * USC.H  (Public header file for the Microsecond Clock package)
 *     This header file has to be included by an application using
 * the USC function calls.
 *
 * Written by:  Arun Nanda    (07/17/91)
 * Modified by R. Butler
 *
 * The following machine-specific libraries need to be linked in
 * with the application using the UST functions:
 *
 */

#ifndef _USC_DEFS_   /* takes care of multiple inclusion of this file */
#define _USC_DEFS_

#ifdef CONVEX
#define SUN
#endif

#ifdef ALPHA
#define DEC5000
#endif

#ifdef SP1
#define RS6000
#endif

#ifdef SP1_EUI
#define RS6000
#endif

#ifdef SP1_EUIH
#define RS6000
#endif

#ifdef DELTA
#define IPSC860
#endif

#ifdef PARAGON
#define IPSC860
#endif

#if defined(SGI_MP)  ||  defined(SGI_CH)  ||  defined(SGI_CH64)
#define SGI
#endif

#if defined(FREEBSD_PPC)
#define FREEBSD
#endif

/* ---------------------
 Global declarations
--------------------- */

typedef unsigned long usc_time_t;

#ifndef VOID
#    if defined(BALANCE) || defined(FX8)
#        define VOID int
#    else
#        define VOID void
#    endif
#endif

/* --------------------------------
   Machine Synonyms
   When P4 and the usc package were designed, there were no truely
   portable operating systems, so using the OS name for the machine
   was a 1-1 mapping.  With various Unix versions, particularly Linux, 
   this is no longer true.
   -------------------------------- */
#if defined(LINUX_PPC) && !defined(LINUX)
#define LINUX
#endif

/* --------------------------------
 Machine dependent declarations
-------------------------------- */

#if defined(MULTIMAX)

     extern unsigned *usc_multimax_timer;

#endif


#if defined(SYMMETRY) || defined(SYMMETRY_PTX)

#ifndef GETUSCLK
#    include <usclkc.h>
#endif
#endif 

extern usc_time_t usc_MD_rollover_val;

/* -----------------------
 user interface macros
----------------------- */

#if defined(MULTIMAX)

#    define usc_clock() ((usc_time_t) *usc_multimax_timer)
#    define usc_rollover_val()  (usc_MD_rollover_val)

#else

#if defined(SYMMETRY) || defined(SYMMETRY_PTX)

#    define usc_clock() ((usc_time_t) getusclk())
#    define usc_rollover_val()  (usc_MD_rollover_val)

#else

#if defined(TC_2000) || defined(TC_2000_TCMP)

#    define usc_clock() usc_MD_clock()
#    define usc_rollover_val()  (usc_MD_rollover_val)

#else

#if defined (NCUBE)

#    define usc_clock() usc_MD_clock()
#    define usc_rollover_val()  (usc_MD_rollover_val)

#else

#if defined (IPSC860)

#    define usc_clock() usc_MD_clock()
#    define usc_rollover_val()  (usc_MD_rollover_val)

#else

#if defined(FX2800)  ||   defined(FX2800_SWITCH)
#    define usc_clock() usc_MD_clock()
#    define usc_rollover_val()  (usc_MD_rollover_val)
#else 

#if defined(SUN) || defined(DEC5000) || defined(HP) \
    || defined(SUN_SOLARIS) || defined(FREEBSD) || defined(LINUX) \
    || defined(I86_SOLARIS) || defined(NETBSD) \
    || defined(IBM3090) || defined(RS6000) \
    || defined(NEXT) || defined(TITAN) || defined(GP_1000) \
    || defined(KSR) \
    || defined(MEIKO_CS2) \
    || defined(SGI) || defined(FX8)

#    define usc_clock() usc_MD_clock()
#    define usc_rollover_val()  (usc_MD_rollover_val * 1000000 - 1)

#else

#    define usc_clock() 0
#    define usc_rollover_val() 0

#endif
#endif
#endif
#endif
#endif
#endif
#endif

/* ----------------------
  function prototypes
---------------------- */
VOID usc_init (void);
usc_time_t usc_MD_clock (void);

#endif
