#ifndef MPID_Wtime

/* Special features of timer for NX */

#define MPID_Wtime(t) *(t)= dclock()
#define MPID_Wtick(t) MPID_CH_Wtick( t )
#define MPID_Wtime_is_global MPID_Time_is_global

extern double dclock (void);
extern int MPID_Time_is_global (void);
extern void MPID_CH_Wtick (double *);

#endif
