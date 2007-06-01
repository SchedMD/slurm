#include "bnr_internal.h"

MPICH_BNR_API int BNR_Info_delete(BNR_Info info, char *key)
{
    BNR_Info prev, curr;
    int done;

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

    prev = info;
    curr = info->next;
    done = 0;

    while (curr) {
	if (!strcmp(curr->key, key)) {
	    free(curr->key);
	    free(curr->value);
		prev->next = curr->next;
	    delete curr;
	    done = 1;
	    break;
	}
	prev = curr;
	curr = curr->next;
    }

    if (!done) {
		return BNR_FAIL;
    }

    return BNR_SUCCESS;
}
