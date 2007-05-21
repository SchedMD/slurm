#include "bnr_internal.h"

/* frees any internal resources
 * No BNR calls may be made after BNR_Finalize
 */
MPICH_BNR_API int BNR_Finalize()
{
	if (g_hMPDPipe != NULL)
	{
		DWORD dwNumWritten;
		WriteFile(g_hMPDPipe, "done\n", strlen("done\n"), &dwNumWritten, NULL);
		FlushFileBuffers(g_hMPDPipe);
		CloseHandle(g_hMPDPipe);
		g_hMPDPipe = NULL;
		CloseHandle(g_hMPDOutputPipe);
		g_hMPDOutputPipe = NULL;
		CloseHandle(g_hMPDEndOutputPipe);
		g_hMPDEndOutputPipe = NULL;
	}

	return BNR_SUCCESS;
}
