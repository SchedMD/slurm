#include "bnr_internal.h"

/* puts attr-value pair for retrieval by other
 * processes in group;  attr is a string of
 * length < BNR_MAXATTRLEN, val is string of
 * length < BNR_MAXVALLEN 
 * rank_advice tells BNR where the Get is likely to be called from.
 * rank_advice can be -1 for no advice.
 */
MPICH_BNR_API int BNR_Put( BNR_Group group, char *attr, char *val, int rank_advice )
{
	DWORD dwNumWritten;
	char pBuffer[256];
	BNR_Group_node *pGroup = (BNR_Group_node*)group;

	if (group == BNR_GROUP_NULL || group == BNR_INVALID_GROUP)
		return BNR_FAIL;

	sprintf(pBuffer, "id %s\nput %s=%s\n", pGroup->pszName, attr, val);
	WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);

	return BNR_SUCCESS;
}
