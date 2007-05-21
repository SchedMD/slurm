#include "bnr_internal.h"
#include "parsecliques.h"

/* returns the members of the clique that rank belongs to in a group, valid clique names are: shm, tcp, and via */
MPICH_BNR_API int BNR_Get_clique(BNR_Group group, char *cliquename, int rank, int max_members, int *num, int members[])
{
	char pszCliqueString[1024] = "";
	int *pTempMembers;
	int i;

	if (cliquename == NULL || num == NULL || members == NULL || group == BNR_INVALID_GROUP || max_members < 1)
		return BNR_FAIL;
	if (group == BNR_GROUP_NULL)
	{
		*num = 0;
		return BNR_SUCCESS;
	}
	if (rank < 0 || rank >= ((BNR_Group_node*)group)->nSize)
		return BNR_FAIL;

	if (stricmp("tcp", cliquename) == 0)
	{
		strcpy(pszCliqueString, "*");
	}
	else if (stricmp("shm", cliquename) == 0)
	{
		if (!GetEnvironmentVariable("BNR_SHM_CLICKS", pszCliqueString, 1024) && !GetEnvironmentVariable("BNR_SHM_CLIQUES", pszCliqueString, 1024))
		{
			*num = 1;
			members[0] = rank;
			return BNR_SUCCESS;
		}
	}
	else if (stricmp("via", cliquename) == 0)
	{
		if (!GetEnvironmentVariable("BNR_VIA_CLICKS", pszCliqueString, 1024) && !GetEnvironmentVariable("BNR_VIA_CLIQUES", pszCliqueString, 1024))
		{
			*num = 0;
			return BNR_SUCCESS;
		}
	}
	else
	{
		return BNR_FAIL;
	}

	if (ParseCliques(pszCliqueString, rank, ((BNR_Group_node*)group)->nSize, num, &pTempMembers) == 0)
	{
		if (*num <= max_members)
		{
			for (i=0; i<*num; i++)
				members[i] = pTempMembers[i];
			delete pTempMembers;
			return BNR_SUCCESS;
		}
		delete pTempMembers;
	}

	*num = 0;
	return BNR_FAIL;
}
