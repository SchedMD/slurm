#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_free(BNR_Info *info)
{
    BNR_Info curr, next;

    if ((*info <= (BNR_Info) 0) || ((*info)->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    curr = (*info)->next;
    delete *info;
    *info = BNR_INFO_NULL;

    while (curr) {
	next = curr->next;
	free(curr->key);
	free(curr->value);
	delete curr;
	curr = next;
    }

    return BNR_SUCCESS;
}
