#include "bnr_internal.h"

/* frees group for re-use. */
MPICH_BNR_API int BNR_Free_group( BNR_Group group )
{
	BNR_Group_node *p = (BNR_Group_node*)group;

	if (p)
	{
		p->nRefCount--;
		if (p->nRefCount == 0)
		{
			// delete the group from the database;
		}
	}
	return BNR_SUCCESS;
}
