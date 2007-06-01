#ifndef MPID_Wtime

/* Special clock for P4 */

#define MPID_Wtime(t) *(t) = p4_usclock()

#define MPID_Wtick(t) MPID_CH_Wtick(t)
extern double p4_usclock (void);
void MPID_CH_Wtick ( double * );
#endif
