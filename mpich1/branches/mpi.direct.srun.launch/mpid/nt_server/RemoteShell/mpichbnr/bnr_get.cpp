#include "bnr_internal.h"

/* matches attr, retrieves corresponding value
 * into val, which is a buffer of
 * length = BNR_MAXVALLEN 
 */
MPICH_BNR_API int BNR_Get( BNR_Group group, char *attr, char *val )
{
	DWORD dwNumWritten;
	char pBuffer[256];
	BNR_Group_node *pGroup = (BNR_Group_node*)group;

	if (group == BNR_GROUP_NULL || group == BNR_INVALID_GROUP)
		return BNR_FAIL;

	sprintf(pBuffer, "id %s\nget %s\n", pGroup->pszName, attr);
	WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
	if (GetString(g_hMPDOutputPipe, val))
	{
		printf("BNR_Get: GetString failed\n");fflush(stdout);
		return BNR_FAIL;
	}
	return BNR_SUCCESS;
}
