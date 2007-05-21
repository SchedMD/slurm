#include "bnr_internal.h"

/* returns rank in group */
MPICH_BNR_API int BNR_Get_rank( BNR_Group group, int *myrank )
{
	if (group != BNR_INVALID_GROUP)
	{
		*myrank = ((BNR_Group_node*)group)->nRank;
		return BNR_SUCCESS;
	}

	*myrank = -1;
	return BNR_FAIL;
}
