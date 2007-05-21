#include "bnr_internal.h"

/* kills processes in group given by group.  This
 * can be used, for example, during spawn_multiple
 * when a spawn fails, to kill off groups already
 * spawned before returning failure 
 */
MPICH_BNR_API int BNR_Kill( BNR_Group group )
{
	return BNR_SUCCESS;
}
