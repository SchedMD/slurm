#include "bnr_internal.h"

/* returns size of group */
MPICH_BNR_API int BNR_Get_size( BNR_Group group, int *size )
{
	if (group != BNR_INVALID_GROUP)
	{
		*size = ((BNR_Group_node*)group)->nSize;
		return BNR_SUCCESS;
	}

	*size = -1;
	return BNR_FAIL;
}
