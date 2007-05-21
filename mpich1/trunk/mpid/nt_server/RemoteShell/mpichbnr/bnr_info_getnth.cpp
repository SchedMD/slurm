#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_get_nthkey(BNR_Info info, int n, char *key)
{
    BNR_Info curr;
    int nkeys, i;

    if ((info <= (BNR_Info) 0) || (info->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    if (!key) {
		return BNR_FAIL;
    }

    curr = info->next;
    nkeys = 0;
    while (curr) {
	curr = curr->next;
	nkeys++;
    }

    if ((n < 0) || (n >= nkeys)) {
		return BNR_FAIL;
    }

    curr = info->next;
    i = 0;
    while (i < n) {
	curr = curr->next;
	i++;
    }
    strcpy(key, curr->key);

    return BNR_SUCCESS;
}
