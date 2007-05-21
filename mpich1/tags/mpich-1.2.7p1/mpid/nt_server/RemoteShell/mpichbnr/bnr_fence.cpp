#include "bnr_internal.h"

/* barriers all processes in group; puts done
 * before the fence are accessible by gets after
 * the fence 
 */
MPICH_BNR_API int BNR_Fence( BNR_Group group)
{
	return BNR_SUCCESS;
}
