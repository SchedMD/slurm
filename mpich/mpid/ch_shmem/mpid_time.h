#ifndef MPID_Wtime

extern double p2p_wtime (void);
#define MPID_Wtime(a) (*(a)) = p2p_wtime();
#define MPID_Wtick MPID_CH_Wtick

void MPID_CH_Wtick ( double * );
#endif
