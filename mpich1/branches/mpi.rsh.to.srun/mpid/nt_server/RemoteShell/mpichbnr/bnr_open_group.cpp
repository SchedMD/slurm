#include "bnr_internal.h"

MPICH_BNR_API int BNR_Open_group( BNR_Group local_group, BNR_Group *new_group )
{
	char pBuffer[100];
	DWORD dwNumWritten;
	BNR_Group_node *pLocal, *pNewGroup;

	pLocal = (BNR_Group_node*)local_group;

	if (pLocal->nRank == 0)
	{
		WriteFile(g_hMPDPipe, "create group\n", strlen("create group\n"), &dwNumWritten, NULL);
		if (GetString(g_hMPDOutputPipe, pBuffer))
		{
			printf("BNR_Open_group: GetString(group id) failed\n");fflush(stdout);
			return BNR_FAIL;
		}
		pNewGroup = AddBNRGroupToList(atoi(pBuffer), -1, 0, (BNR_Group_node*)local_group);

		if (strlen(pLocal->pszName))
		{
			sprintf(pBuffer, "id %s\n", pLocal->pszName);
			WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
			sprintf(pBuffer, "put opened=%d\n", pNewGroup->nID);
			WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
		}
		else
		{
			printf("BNR_Open_group: Local group doesn't have a name.\n");
			return BNR_FAIL;
		}
	}
	else
	{
		if (strlen(pLocal->pszName))
		{
			sprintf(pBuffer, "id %s\n", pLocal->pszName);
			WriteFile(g_hMPDPipe, pBuffer, strlen(pBuffer), &dwNumWritten, NULL);
			WriteFile(g_hMPDPipe, "get opened\n", strlen("get opened\n"), &dwNumWritten, NULL);
			if (GetString(g_hMPDOutputPipe, pBuffer))
			{
				printf("BNR_Open_group: GetString(opened id) failed\n");fflush(stdout);
				return BNR_FAIL;
			}
			pNewGroup = AddBNRGroupToList(atoi(pBuffer), -1, 0, (BNR_Group_node*)local_group);
		}
		else
		{
			printf("BNR_Open_group: Local group doesn't have a name.\n");
			return BNR_FAIL;
		}
	}

	*new_group = pNewGroup;

	return BNR_SUCCESS;
}
