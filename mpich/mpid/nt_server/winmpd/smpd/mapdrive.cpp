#include "mpdimpl.h"
#include <winsock2.h>
#include <windows.h>
#include "Translate_Error.h"

static bool UnmapDrive(char *pszDrive, bool bImpersonate, char *pszError);
static bool MapDrive(char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword, bool bLogon, char *pszError);

struct DriveMapStruct
{
    DriveMapStruct();

    int nRefCount;
    char pszDrive[10];
    char pszShare[MAX_PATH];
    HANDLE hUser;
    bool bUnmap;
    DriveMapStruct *pNext;
};

DriveMapStruct::DriveMapStruct()
{
    nRefCount = 1;
    pszDrive[0] = '\0';
    hUser = NULL;
    pNext = NULL;
    bUnmap = true;
}

static DriveMapStruct *g_pDriveList = NULL;

static bool AlreadyMapped(char *pszDrive, char *pszShare, bool *pMatched)
{
    DriveMapStruct *p;
    if (g_pDriveList == NULL)
	return false;
    p = g_pDriveList;
    while (p)
    {
	if (pszDrive[0] == p->pszDrive[0])
	{
	    if ((stricmp(pszShare, p->pszShare) == 0))
	    {
		p->nRefCount++;
		*pMatched = true;
	    }
	    else
		*pMatched = false;
	    return true;
	}
	p = p->pNext;
    }
    return false;
}

static bool CompareHosts(char *pszHost1, char *pszHost2)
{
    unsigned long ip1, ip2;
    struct hostent *pH;

    pH = gethostbyname(pszHost1);
    if (pH == NULL)
	return false;

    ip1 = (unsigned long)(pH->h_addr_list[0]);

    pH = gethostbyname(pszHost2);
    if (pH == NULL)
	return false;

    ip2 = (unsigned long)(pH->h_addr_list[0]);

    return (ip1 == ip2);
}

static BOOL EnumerateDisksFunc(LPNETRESOURCE lpnr, DWORD dwScope, DWORD dwType, char *pszDrive, char *pszShare, bool *pbFound, bool *pbMatched)
{
    DWORD dwResult, dwResultEnum;
    HANDLE hEnum;
    DWORD cbBuffer = 16384;      // 16K is a good size
    DWORD cEntries = -1;         // enumerate all possible entries
    LPNETRESOURCE lpnrLocal;     // pointer to enumerated structures
    DWORD i;

    dwResult = WNetOpenEnum(
	dwScope,
	dwType,
	0,        // enumerate all resources
	lpnr,     // NULL first time the function is called
	&hEnum);  // handle to the resource
    
    if (dwResult != NO_ERROR)
	return FALSE;

    lpnrLocal = (LPNETRESOURCE) GlobalAlloc(GPTR, cbBuffer);
    
    do
    {  
	ZeroMemory(lpnrLocal, cbBuffer);
	dwResultEnum = WNetEnumResource(
	    hEnum,      // resource handle
	    &cEntries,  // defined locally as -1
	    lpnrLocal,  // LPNETRESOURCE
	    &cbBuffer); // buffer size
	// If the call succeeds, loop through the structures.
	if (dwResultEnum == NO_ERROR)
	{
	    for(i = 0; i < cEntries; i++)
	    {
		if (lpnrLocal[i].lpLocalName && lpnrLocal[i].lpRemoteName)
		{
		    if (toupper(*lpnrLocal[i].lpLocalName) == *pszDrive)
		    {
			*pbFound = true;
			if ((stricmp(lpnrLocal[i].lpRemoteName, pszShare) == 0))
			{
			    *pbMatched = true;
			}
			else
			{
			    char *pPath1, *pPath2;
			    pPath1 = strstr(&lpnrLocal[i].lpRemoteName[2], "\\");
			    if (pPath1 != NULL)
			    {
				pPath1++; // advance over the \ character
				pPath2 = strstr(&pszShare[2], "\\");
				if (pPath2 != NULL)
				{
				    pPath2++; // advance over the \ character
				    if (stricmp(pPath1, pPath2) == 0)
				    {
					char pszHost1[50], pszHost2[50];
					int nLength1, nLength2;
					nLength1 = pPath1 - &lpnrLocal[i].lpRemoteName[2] - 1;
					nLength2 = pPath2 - &pszShare[2] - 1;
					strncpy(pszHost1, &lpnrLocal[i].lpRemoteName[2], nLength1);
					strncpy(pszHost2, &pszShare[2], nLength2);
					pszHost1[nLength1] = '\0';
					pszHost2[nLength2] = '\0';
					if (CompareHosts(pszHost1, pszHost2))
					    *pbMatched = true;
				    }
				}
			    }
			}
		    }
		}
		
		// If the NETRESOURCE structure represents a container resource, 
		//  call the EnumerateDisksFunc function recursively.
		if(RESOURCEUSAGE_CONTAINER == (lpnrLocal[i].dwUsage & RESOURCEUSAGE_CONTAINER))
		    EnumerateDisksFunc(&lpnrLocal[i], dwScope, dwType, pszDrive, pszShare, pbFound, pbMatched);
	    }
	}
	else if (dwResultEnum != ERROR_NO_MORE_ITEMS)
	{
	    break;
	}
    } while(dwResultEnum != ERROR_NO_MORE_ITEMS);

    GlobalFree((HGLOBAL)lpnrLocal);

    dwResult = WNetCloseEnum(hEnum);
    
    if(dwResult != NO_ERROR)
	return FALSE;
    
    return TRUE;
}

static bool MatchesExistingMapping(char *pszDrive, char *pszShare)
{
    bool bFound = false;
    bool bMatched = false;
    char ch;

    if (pszDrive == NULL || pszShare == NULL)
	return false;

    ch = toupper(*pszDrive);
    EnumerateDisksFunc(NULL, RESOURCE_CONNECTED, RESOURCETYPE_DISK, &ch, pszShare, &bFound, &bMatched);
    if (bMatched)
	return true;
    EnumerateDisksFunc(NULL, RESOURCE_REMEMBERED, RESOURCETYPE_DISK, &ch, pszShare, &bFound, &bMatched);
    if (bMatched)
	return true;

    // If it was not found, assume that it matches
    return !bFound;
}

static void RemoveDriveStruct(char *pszDrive)
{
    DriveMapStruct *p, *pTrailer;

    pTrailer = p = g_pDriveList;
    while (p)
    {
	if (p->pszDrive[0] == pszDrive[0])
	{
	    p->nRefCount--;
	    if (p->nRefCount == 0)
	    {
		if (pTrailer != p)
		    pTrailer->pNext = p->pNext;
		if (g_pDriveList == p)
		    g_pDriveList = g_pDriveList->pNext;
		LoseTheUser(p->hUser);
		delete p;
	    }
	    return;
	}
	if (pTrailer != p)
	    pTrailer = pTrailer->pNext;
	p = p->pNext;
    }
}

void FinalizeDriveMaps()
{
    while (g_pDriveList)
	RemoveDriveStruct(g_pDriveList->pszDrive);
}

static bool ParseDriveShareAccountPassword(char *str, char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword)
{
    pszDrive[0] = str[0];
    pszDrive[1] = ':';
    pszDrive[2] = '\0';
    while (*str != '\\')
	str++;
    if (strstr(str, ":"))
    {
	while (*str != ':')
	    *pszShare++ = *str++;
	*pszShare = '\0';
	str++;
	if (!strstr(str, ":"))
	    return false;
	while (*str != ':')
	    *pszAccount++ = *str++;
	*pszAccount = '\0';
	str++;
	strcpy(pszPassword, str);
    }
    else
    {
	strcpy(pszShare, str);
	*pszAccount = '\0';
    }
    return true;
}

bool MapUserDrives(char *pszMap, char *pszAccount, char *pszPassword, char *pszError)
{
    char pszDrive[3];
    char pszShare[MAX_PATH];
    char ipszAccount[100];
    char ipszPassword[100];
    char *token;
    char *temp = strdup(pszMap);

    token = strtok(temp, ";\n");
    if (token == NULL)
	return true;
    while (token != NULL)
    {
	ipszAccount[0] = '\0';
	if (ParseDriveShareAccountPassword(token, pszDrive, pszShare, ipszAccount, ipszPassword))
	{
	    if (ipszAccount[0]  != '\0')
	    {
		if (!MapDrive(pszDrive, pszShare, ipszAccount, ipszPassword, false, pszError))
		{
		    free(temp);
		    //err_printf("MapUserDrives: iMapDrive(%s, %s, %s, ... ) failed, %s\n", pszDrive, pszShare, ipszAccount, pszError);
		    return false;
		}
	    }
	    else
	    {
		if (!MapDrive(pszDrive, pszShare, pszAccount, pszPassword, false, pszError))
		{
		    free(temp);
		    //err_printf("MapUserDrives: MapDrive(%s, %s, %s, ... ) failed, %s\n", pszDrive, pszShare, pszAccount, pszError);
		    return false;
		}
	    }
	}
	token = strtok(NULL, ";\n");
    }
    free(temp);

    return true;
}

bool UnmapUserDrives(char *pszMap)
{
    char pszError[256];
    char pszDrive[3];
    char pszShare[MAX_PATH];
    char pszAccount[100];
    char pszPassword[100];
    char *temp = strdup(pszMap);
    char *token;

    token = strtok(temp, ";\n");
    if (token == NULL)
	return true;
    while (token != NULL)
    {
	if (ParseDriveShareAccountPassword(token, pszDrive, pszShare, pszAccount, pszPassword))
	{
	    if (!UnmapDrive(pszDrive, false, pszError))
		return false;
	}
	token = strtok(NULL, ";\n");
    }
    free(temp);

    return false;
}

static bool MapDrive(char *pszDrive, char *pszShare, char *pszAccount, char *pszPassword, bool bLogon, char *pszError)
{
    char pszDriveLetter[3];
    DWORD dwResult;
    char pszName[1024];
    char pszProvider[256];
    NETRESOURCE net;
    HANDLE hUser = (HANDLE)-1;
    int nError;
    bool bMatched;

    if (pszDrive == NULL)
    {
	strcpy(pszError, "Invalid drive string");
	return false;
    }
    pszDriveLetter[0] = pszDrive[0];
    pszDriveLetter[1] = ':';
    pszDriveLetter[2] = '\0';

    memset(&net, 0, sizeof(NETRESOURCE));
    net.lpLocalName = pszDriveLetter;
    net.lpRemoteName = pszShare;
    net.dwType = RESOURCETYPE_DISK;
    //net.dwType = RESOURCETYPE_ANY;
    net.lpProvider = NULL;

    if (AlreadyMapped(pszDriveLetter, pszShare, &bMatched))
    {
	if (bMatched)
	    return true;
	sprintf(pszError, "Drive %s already mapped.", pszDrive);
	//err_printf("MapDrive failed, drive is already mapped\n", pszError);
	return false;
    }

    if (bLogon)
    {
	hUser = BecomeUser(pszAccount, pszPassword, &nError);
	if (hUser == (HANDLE)-1)
	{
	    Translate_Error(nError, pszError, "BecomeUser failed: ");
	    //err_printf("MapDrive failed, error: %s\n", pszError);
	    return false;
	}
    }

    if (pszAccount != NULL)
    {
	if (*pszAccount == '\0')
	{
	    // Change empty username to NULL pointer to work with WNetAddConnection2.
	    pszAccount = NULL;
	    pszPassword = NULL;
	}
    }

    dwResult = WNetAddConnection2(&net, pszPassword, pszAccount, CONNECT_REDIRECT);

    if (dwResult == NO_ERROR)
    {
	DriveMapStruct *p = new DriveMapStruct;
	strcpy(p->pszDrive, pszDriveLetter);
	strncpy(p->pszShare, pszShare, MAX_PATH);
	p->hUser = hUser;
	p->pNext = g_pDriveList;
	g_pDriveList = p;
	if (bLogon) RevertToSelf();
	return true;
    }

    switch (dwResult)
    {
    case ERROR_ACCESS_DENIED:
	strcpy(pszError, "Access to the network resource was denied.");
	break;
    case ERROR_ALREADY_ASSIGNED:
	if (MatchesExistingMapping(pszDriveLetter, pszShare))
	{
	    DriveMapStruct *p = new DriveMapStruct;
	    strcpy(p->pszDrive, pszDriveLetter);
	    strncpy(p->pszShare, pszShare, MAX_PATH);
	    p->hUser = hUser;
	    p->bUnmap = false; // don't unmap this drive since it was mapped outside mpd
	    p->pNext = g_pDriveList;
	    g_pDriveList = p;
	    if (bLogon) RevertToSelf();
	    return true;
	}
	else
	    sprintf(pszError, "The local device '%s' is already connected to a network resource.", pszDriveLetter);
	break;
    case ERROR_BAD_DEV_TYPE:
	strcpy(pszError, "The type of local device and the type of network resource do not match.");
	break;
    case ERROR_BAD_DEVICE:
	sprintf(pszError, "The value '%s' is invalid.", pszDriveLetter);
	break;
    case ERROR_BAD_NET_NAME:
	sprintf(pszError, "The value '%s' is not acceptable to any network resource provider because the resource name is invalid, or because the named resource cannot be located.", pszShare);
	break;
    case ERROR_BAD_PROFILE:
	strcpy(pszError, "The user profile is in an incorrect format.");
	break;
    case ERROR_BAD_PROVIDER:
	strcpy(pszError, "The value specified by the lpProvider member does not match any provider.");
	break;
    case ERROR_BUSY:
	strcpy(pszError, "The router or provider is busy, possibly initializing. The caller should retry.");
	break;
    case ERROR_CANCELLED:
	strcpy(pszError, "The attempt to make the connection was canceled by the user through a dialog box from one of the network resource providers, or by a called resource.");
	break;
    case ERROR_CANNOT_OPEN_PROFILE:
	strcpy(pszError, "The system is unable to open the user profile to process persistent connections.");
	break;
    case ERROR_DEVICE_ALREADY_REMEMBERED:
	if (MatchesExistingMapping(pszDriveLetter, pszShare))
	{
	    DriveMapStruct *p = new DriveMapStruct;
	    strcpy(p->pszDrive, pszDriveLetter);
	    strncpy(p->pszShare, pszShare, MAX_PATH);
	    p->hUser = hUser;
	    p->bUnmap = false; // don't unmap this drive since it was mapped outside mpd
	    p->pNext = g_pDriveList;
	    g_pDriveList = p;
	    if (bLogon) RevertToSelf();
	    return true;
	}
	else
	    sprintf(pszError, "An entry for the device '%s' is already in the user profile.", pszDriveLetter);
	break;
    case ERROR_EXTENDED_ERROR:
	if (WNetGetLastError(&dwResult, pszName, 1024, pszProvider, 256) == NO_ERROR)
	    sprintf(pszError, "'%s' returned this error: %d, %s", pszProvider, dwResult, pszName);
	else
	    strcpy(pszError, "A network-specific error occurred.");
	break;
    case ERROR_INVALID_PASSWORD:
	strcpy(pszError, "The specified password is invalid.");
	break;
    case ERROR_NO_NET_OR_BAD_PATH:
	strcpy(pszError, "The operation could not be completed, either because a network component is not started, or because the specified resource name is not recognized.");
	break;
    case ERROR_NO_NETWORK:
	strcpy(pszError, "The network is unavailable.");
	break;
    default:
	Translate_Error(dwResult, pszError);
	err_printf("MapDrive: unknown error %d\n", dwResult);
	break;
    }

    if (bLogon) RevertToSelf();

    if (hUser != (HANDLE)-1)
	LoseTheUser(hUser);

    //err_printf("MapDrive failed, error: %s\n", pszError);
    return false;
}

static bool UnmapDrive(char *pszDrive, bool bImpersonate, char *pszError)
{
    char pszName[1024];
    char pszProvider[256];
    char pszDriveLetter[3];
    DWORD dwResult;
    HANDLE hUser = NULL;
    DriveMapStruct *p = g_pDriveList;

    if (pszDrive == NULL)
	return false;
    pszDriveLetter[0] = pszDrive[0];
    pszDriveLetter[1] = ':';
    pszDriveLetter[2] = '\0';

    while (p)
    {
	if (p->pszDrive[0] == pszDrive[0])
	{
	    if (p->nRefCount > 1)
	    {
		p->nRefCount--;
		return true;
	    }
	    hUser = p->hUser;
	    break;
	}
	p = p->pNext;
    }
    if (hUser == NULL)
    {
	strcpy(pszError, "Drive not previously mapped with map call.");
	return false;
    }

    if (bImpersonate)
    {
	WaitForSingleObject(g_hLaunchMutex, 10000);
	ImpersonateLoggedOnUser(hUser);
    }

    if (p->bUnmap)
    {
	dwResult = WNetCancelConnection2(pszDriveLetter, 
	    CONNECT_UPDATE_PROFILE, // This option makes sure that the connection is not re-established at the next user logon
	    TRUE);
    }
    else
	dwResult = NO_ERROR;

    if (bImpersonate)
    {
	RevertToSelf();
	ReleaseMutex(g_hLaunchMutex);
    }

    if (dwResult == NO_ERROR)
    {
	RemoveDriveStruct(pszDriveLetter);
	return true;
    }

    switch (dwResult)
    {
    case ERROR_BAD_PROFILE:
	strcpy(pszError, "The user profile is in an incorrect format.");
	break;
    case ERROR_CANNOT_OPEN_PROFILE:
	strcpy(pszError, "The system is unable to open the user profile to process persistent connections.");
	break;
    case ERROR_DEVICE_IN_USE:
	strcpy(pszError, "The device is in use by an active process and cannot be disconnected.");
	break;
    case ERROR_EXTENDED_ERROR:
	if (WNetGetLastError(&dwResult, pszName, 1024, pszProvider, 256) == NO_ERROR)
	    sprintf(pszError, "'%s' returned this error: %d, %s", pszProvider, dwResult, pszName);
	else
	    strcpy(pszError, "A network-specific error occurred.");
	break;
    case ERROR_NOT_CONNECTED:
	sprintf(pszError, "'%s' is not a redirected device, or the system is not currently connected to '%s'.", pszDriveLetter, pszDriveLetter);
	break;
    case ERROR_OPEN_FILES:
	strcpy(pszError, "There are open files, the drive cannot be disconnected.");
	break;
    default:
	Translate_Error(dwResult, pszError);
	break;
    }

    if (hUser != (HANDLE)-1)
	LoseTheUser(hUser);
    return false;
}
