#ifndef MPID_Wtime

/* Special features of timer for NX */

#ifndef ANSI_ARGS
#if defined(__STDC__) || defined(__cplusplus)
#define ANSI_ARGS(a) a
#else
#define ANSI_ARGS(a) ()
#endif
#endif

#define MPID_Wtime(t) *(t)= MPID_Cenju3_Time ()
#define MPID_Wtick(t) MPID_CH_Wtick( t )
#define MPID_Wtime_is_global MPID_Time_is_global

extern double MPID_Cenju3_Time ();
extern int MPID_Time_is_global ANSI_ARGS((void));
extern void MPID_CH_Wtick ANSI_ARGS((double *));

#endif
