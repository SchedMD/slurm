#include "bnr_internal.h"

/* closes an open group */
MPICH_BNR_API int BNR_Close_group( BNR_Group group )
{
	DWORD dwNumWritten;
	char pBuffer[256];
	BNR_Group_node *pGroup = (BNR_Group_node*)group;

	if (group == BNR_GROUP_NULL || group == BNR_INVALID_GROUP)
		return BNR_FAIL;

	sprintf(pBuffer, "id %s\nput size=%d\n", pGroup->pszName, pGroup->nSize);
	WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);

	return BNR_SUCCESS;
}
