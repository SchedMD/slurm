#ifndef MPID_Wtime

/* Special features of timer for MPL (allows access to global clock) */

#ifdef MPID_TB2_TIMER
#define MPID_Wtime(t) *(t)= MPID_GTime()
#define MPID_Wtick(t) MPID_CH_Wtick( t )
#define MPID_Wtime_is_global MPID_Time_is_global

extern double MPID_GTime (void);
extern int MPID_Time_is_global (void);
extern void MPID_CH_Wtick (double *);
#else
#define MPID_Wtime(t) MPID_CH_Wtime(t)
#define MPID_Wtick(t) MPID_CH_Wtick( t )
#define MPID_Wtime_is_global 0

extern double MPID_CH_Wtime (double *);
extern void MPID_CH_Wtick (double *);
#endif

#endif
