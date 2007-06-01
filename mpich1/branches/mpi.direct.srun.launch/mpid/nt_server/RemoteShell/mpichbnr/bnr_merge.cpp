#include "bnr_internal.h"

/* calling process must be in the local group and
 * must not be in the remote group.  Collective 
 * over the union of the two groups. 
 */
MPICH_BNR_API int BNR_Merge( BNR_Group local_group, BNR_Group remote_group, BNR_Group *new_group )
{
	// Check parameters.
	if (local_group == BNR_GROUP_NULL || local_group == BNR_INVALID_GROUP || remote_group == BNR_INVALID_GROUP)
	{
		*new_group = BNR_INVALID_GROUP;
		return BNR_FAIL;
	}

	// Handle the case of merging with a NULL group
	if (remote_group == BNR_GROUP_NULL)
	{
		*new_group = local_group;
		return BNR_SUCCESS;
	}

	*new_group = MergeBNRGroupToList((BNR_Group_node*)local_group, (BNR_Group_node*)remote_group);

	return (*new_group == BNR_INVALID_GROUP) ? BNR_FAIL : BNR_SUCCESS;
}
