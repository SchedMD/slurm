#include "bnr_internal.h"

/* returns 1 if no parent all */
MPICH_BNR_API int BNR_Get_parent( BNR_Group *parent_group )
{
	char pBuffer[100];

	if (g_bnrParent != BNR_INVALID_GROUP)
	{
		*parent_group = g_bnrParent;
		return BNR_SUCCESS;
	}

	if (GetEnvironmentVariable("BNR_PARENT", pBuffer, 100))
	{
		int nParentSize = -1;
		int nParent = atoi(pBuffer);
		if (GetEnvironmentVariable("BNR_PARENT_SIZE", pBuffer, 100))
			nParentSize = atoi(pBuffer);
		g_bnrParent = AddBNRGroupToList(nParent, -1, nParentSize);
		*parent_group = g_bnrParent;
		return BNR_SUCCESS;
	}

	*parent_group = BNR_INVALID_GROUP;
	return BNR_FAIL;
}
