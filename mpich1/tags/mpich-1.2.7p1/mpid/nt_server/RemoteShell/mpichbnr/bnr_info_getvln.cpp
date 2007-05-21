#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_get_valuelen(BNR_Info info, char *key, int *valuelen, int *flag)
{
    BNR_Info curr;

    if ((info <= (BNR_Info) 0) || (info->cookie != BNR_INFO_COOKIE)) {
		return BNR_FAIL;
    }

    if (!key) {
		return BNR_FAIL;
    }

    if (strlen(key) > BNR_MAX_INFO_KEY) {
		return BNR_FAIL;
    }

    if (!strlen(key)) {
		return BNR_FAIL;
    }


    curr = info->next;
    *flag = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    *valuelen = strlen(curr->value);
	    *flag = 1;
	    break;
	}
	curr = curr->next;
    }

    return BNR_SUCCESS;
}
