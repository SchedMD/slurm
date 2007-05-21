#ifndef MPIRUNBNR_H
#define MPIRUNBNR_H

#include "bnr.h"

extern bool g_bUseBNR;
extern HANDLE g_hBNRProcessesFinishedEvent;
extern LONG g_nNumBNRProcessesRemaining;

int ExitBNRProcess(BNR_Group group,int rank,int exit_code);

#endif
