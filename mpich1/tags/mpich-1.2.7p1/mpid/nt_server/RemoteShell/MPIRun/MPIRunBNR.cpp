#include "global.h"
#include "MPIRunBNR.h"
#include <stdio.h>

HANDLE g_hBNRProcessesFinishedEvent = NULL;
LONG g_nNumBNRProcessesRemaining = 0;

int ExitBNRProcess(BNR_Group group,int rank,int exit_code)
{
	//printf("Rank %d has exited with code: %d\n", rank, exit_code);
	if (InterlockedDecrement(&g_nNumBNRProcessesRemaining) == 0)
		SetEvent(g_hBNRProcessesFinishedEvent);
	return 0;
}
