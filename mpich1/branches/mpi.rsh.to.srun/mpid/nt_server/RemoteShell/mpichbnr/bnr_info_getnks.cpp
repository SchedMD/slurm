#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_get_nkeys(BNR_Info info, int *nkeys)
{
    BNR_Info curr;

    if ((info <= (BNR_Info) 0) || (info->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    curr = info->next;
    *nkeys = 0;

    while (curr) {
	curr = curr->next;
	(*nkeys)++;
    }

    return BNR_SUCCESS;
}
