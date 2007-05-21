#include "nt_global_cpp.h"

// Global function pointer declaration
#define BNR_FUNCTION_DECLARATIONS
#include "bnr.h"
#undef BNR_FUNCTION_DECLARATIONS

BNR_Group g_myBNRgroup = BNR_GROUP_NULL;

bool LoadBNRFunctions()
{
	HMODULE hBNRLib;
	char pszLibrary[1024];
	
	// First initialize everythting to NULL
	BNR_Init = NULL;
	BNR_Finalize = NULL;
	BNR_Get_group = NULL;
	BNR_Get_parent = NULL;
	BNR_Get_rank = NULL;
	BNR_Get_size = NULL;
	BNR_Open_group = NULL;
	BNR_Close_group = NULL;
	BNR_Free_group = NULL;
	BNR_Merge = NULL;
	BNR_Spawn = NULL;
	BNR_Kill = NULL;
	BNR_Put = NULL;
	BNR_Get = NULL;
	BNR_Fence = NULL;
	BNR_Deposit = NULL;
	BNR_Withdraw = NULL;
	BNR_Lookup = NULL;
	
	BNR_Info_set = NULL;
	BNR_Info_get_valuelen = NULL;
	BNR_Info_get_nthkey = NULL;
	BNR_Info_get_nkeys = NULL;
	BNR_Info_get = NULL;
	BNR_Info_free = NULL;
	BNR_Info_dup = NULL;
	BNR_Info_delete = NULL;
	BNR_Info_create = NULL;

	if (!GetEnvironmentVariable("MPICH_BNR_LIB", pszLibrary, 1024))
	{
		// Try to load the default library:
		//strcpy(pszLibrary, "mpichbnr.dll");
		// or just bail out:
		return false;
	}

	hBNRLib = LoadLibrary(pszLibrary);

	if (hBNRLib == NULL)
		return false;

	// Add code to check if the return values are NULL ...
	BNR_Init = (int (BNR_CALL *)())GetProcAddress(hBNRLib, "BNR_Init");
	if (BNR_Init == NULL) DPRINTF(("BNR_Init == NULL\n"));
	BNR_Finalize = (int (BNR_CALL *)())GetProcAddress(hBNRLib, "BNR_Finallize");
	if (BNR_Finalize == NULL) DPRINTF(("BNR_Finalize == NULL\n"));
	BNR_Get_group = (int (BNR_CALL *)( BNR_Group *mygroup ))GetProcAddress(hBNRLib, "BNR_Get_group");
	if (BNR_Get_group == NULL) DPRINTF(("BNR_Get_group == NULL\n"));
	BNR_Get_parent = (int (BNR_CALL *)( BNR_Group *parent_group ))GetProcAddress(hBNRLib, "BNR_Get_parent");
	if (BNR_Get_parent == NULL) DPRINTF(("BNR_Get_parent == NULL\n"));
	BNR_Get_rank = (int (BNR_CALL *)( BNR_Group group, int *myrank ))GetProcAddress(hBNRLib, "BNR_Get_rank");
	if (BNR_Get_rank == NULL) DPRINTF(("BNR_Get_rank == NULL\n"));
	BNR_Get_size = (int (BNR_CALL *)( BNR_Group group, int *mysize ))GetProcAddress(hBNRLib, "BNR_Get_size");
	if (BNR_Get_size == NULL) DPRINTF(("BNR_Get_size == NULL\n"));
	BNR_Open_group = (int (BNR_CALL *)( BNR_Group local_group, BNR_Group *new_group ))GetProcAddress(hBNRLib, "BNR_Open_group");
	if (BNR_Open_group == NULL) DPRINTF(("BNR_Open_group == NULL\n"));
	BNR_Close_group = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Close_group");
	if (BNR_Close_group == NULL) DPRINTF(("BNR_Close_group == NULL\n"));
	BNR_Free_group = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Free_group");
	if (BNR_Free_group == NULL) DPRINTF(("BNR_Free_group == NULL\n"));
	BNR_Merge = (int (BNR_CALL *)(BNR_Group local_group, BNR_Group remote_group, BNR_Group *new_group ))GetProcAddress(hBNRLib, "BNR_Merge");
	if (BNR_Merge == NULL) DPRINTF(("BNR_Merge == NULL\n"));
	BNR_Spawn = (int (BNR_CALL *)(BNR_Group remote_group, int count, char *command, char *argv, char *env, BNR_Info info, int (notify_fn)(BNR_Group group, int rank, int exit_code) ))GetProcAddress(hBNRLib, "BNR_Spawn");
	if (BNR_Spawn == NULL) DPRINTF(("BNR_Spawn == NULL\n"));
	BNR_Kill = (int (BNR_CALL *)( BNR_Group group ))GetProcAddress(hBNRLib, "BNR_Kill");
	if (BNR_Kill == NULL) DPRINTF(("BNR_Kill == NULL\n"));
	BNR_Put = (int (BNR_CALL *)( BNR_Group group, char *attr, char *val, int rank_advice ))GetProcAddress(hBNRLib, "BNR_Put");
	if (BNR_Put == NULL) DPRINTF(("BNR_Put == NULL\n"));
	BNR_Get = (int (BNR_CALL *)( BNR_Group group, char *attr, char *val ))GetProcAddress(hBNRLib, "BNR_Get");
	if (BNR_Get == NULL) DPRINTF(("BNR_Get == NULL\n"));
	BNR_Fence = (int (BNR_CALL *)( BNR_Group ))GetProcAddress(hBNRLib, "BNR_Fence");
	if (BNR_Fence == NULL) DPRINTF(("BNR_Fence == NULL\n"));
	BNR_Deposit = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Deposit");
	if (BNR_Deposit == NULL) DPRINTF(("BNR_Deposit == NULL\n"));
	BNR_Withdraw = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Withdraw");
	if (BNR_Withdraw == NULL) DPRINTF(("BNR_Withdraw == NULL\n"));
	BNR_Lookup = (int (BNR_CALL *)( char *attr, char *value ))GetProcAddress(hBNRLib, "BNR_Lookup");
	if (BNR_Lookup == NULL) DPRINTF(("BNR_Lookup == NULL\n"));
	
	BNR_Info_set = (int (BNR_CALL *)(BNR_Info info, char *key, char *value))GetProcAddress(hBNRLib, "BNR_Info_set");
	if (BNR_Info_set == NULL) DPRINTF(("BNR_Info_set == NULL\n"));
	BNR_Info_get_valuelen = (int (BNR_CALL *)(BNR_Info info, char *key, int *valuelen, int *flag))GetProcAddress(hBNRLib, "BNR_Info_get_valuelen");
	if (BNR_Info_get_valuelen == NULL) DPRINTF(("BNR_Info_get_valuelen == NULL\n"));
	BNR_Info_get_nthkey = (int (BNR_CALL *)(BNR_Info info, int n, char *key))GetProcAddress(hBNRLib, "BNR_Info_get_nthkey");
	if (BNR_Info_get_nthkey == NULL) DPRINTF(("BNR_Info_get_nthkey == NULL\n"));
	BNR_Info_get_nkeys = (int (BNR_CALL *)(BNR_Info info, int *nkeys))GetProcAddress(hBNRLib, "BNR_Info_get_nkeys");
	if (BNR_Info_get_nkeys == NULL) DPRINTF(("BNR_Info_get_nkeys == NULL\n"));
	BNR_Info_get = (int (BNR_CALL *)(BNR_Info info, char *key, int valuelen, char *value, int *flag))GetProcAddress(hBNRLib, "BNR_Info_get");
	if (BNR_Info_get == NULL) DPRINTF(("BNR_Info_get == NULL\n"));
	BNR_Info_free = (int (BNR_CALL *)(BNR_Info *info))GetProcAddress(hBNRLib, "BNR_Info_free");
	if (BNR_Info_free == NULL) DPRINTF(("BNR_Info_free == NULL\n"));
	BNR_Info_dup = (int (BNR_CALL *)(BNR_Info info, BNR_Info *newinfo))GetProcAddress(hBNRLib, "BNR_Info_dup");
	if (BNR_Info_dup == NULL) DPRINTF(("BNR_Info_dup == NULL\n"));
	BNR_Info_delete = (int (BNR_CALL *)(BNR_Info info, char *key))GetProcAddress(hBNRLib, "BNR_Info_delete");
	if (BNR_Info_delete == NULL) DPRINTF(("BNR_Info_delete == NULL\n"));
	BNR_Info_create = (int (BNR_CALL *)(BNR_Info *info))GetProcAddress(hBNRLib, "BNR_Info_create");
	if (BNR_Info_create == NULL) DPRINTF(("BNR_Info_create == NULL\n"));

	return true;
}
