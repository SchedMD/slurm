#include "bnr_internal.h"

/* returns primary group id assigned at creation */
MPICH_BNR_API int BNR_Get_group( BNR_Group *mygroup )
{
	if (g_bnrGroup != BNR_INVALID_GROUP)
	{
		*mygroup = g_bnrGroup;
		return BNR_SUCCESS;
	}

	*mygroup = BNR_INVALID_GROUP;
	return BNR_FAIL;
}