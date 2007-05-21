#include "mpdimpl.h"

void CreateMPDRegistry()
{
    HKEY tkey;
    DWORD result;

    // Open the root key
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	int error = GetLastError();
	//err_printf("Unable to create the mpd registry key, error %d\n", error);
	return;
    }
    RegCloseKey(tkey);
}

void CleanMPDRegistry()
{
    if (RegDeleteKey(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY) != ERROR_SUCCESS)
    {
	int error = GetLastError();
	if (error)
	    err_printf("Unable to remove the MPD registry key, error %d\n", error);
    }
}

bool ReadMPDRegistry(char *name, char *value, bool bPrintError /*= true*/ )
{
    HKEY tkey;
    DWORD len, result;

    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, 
	KEY_READ,
	&tkey) != ERROR_SUCCESS)
    {
	if (bPrintError)
	    err_printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", GetLastError());
	return false;
    }

    len = MAX_CMD_LENGTH;
    result = RegQueryValueEx(tkey, name, 0, NULL, (unsigned char *)value, &len);
    if (result != ERROR_SUCCESS)
    {
	if (bPrintError)
	    //warning_printf("Unable to read the mpd registry key '%s', error %d\n", name, GetLastError());
	    dbg_printf("Unable to read the mpd registry key '%s', error %d\n", name, GetLastError());
	RegCloseKey(tkey);
	return false;
    }

    RegCloseKey(tkey);
    return true;
}

void MPDRegistryToString(char *pszStr, int length)
{
    HKEY tkey;
    DWORD len, result, len2;
    DWORD nMaxKeyLen, nMaxValueLen, nNumKeys, dwType;
    char *pszKey, *pszValue;
    char pszNum[10];

    if (length < 1)
    {
	err_printf("MPDRegistryToString: string too short\n");
	return;
    }

    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, KEY_ALL_ACCESS, &tkey) != ERROR_SUCCESS)
    {
	err_printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", GetLastError());
	return;
    }

    result = RegQueryInfoKey(tkey, NULL, NULL, NULL, NULL, NULL, NULL, &nNumKeys, &nMaxKeyLen, &nMaxValueLen, NULL, NULL);
    if (result != ERROR_SUCCESS)
    {
	err_printf("Unable to query the mpd registry key, error %d\n", GetLastError());
	RegCloseKey(tkey);
	return;
    }

    pszKey = new char[nMaxKeyLen+1];
    pszValue = new char[nMaxValueLen+1];
    pszStr[0] = '\0';

    for (DWORD i=0; i<nNumKeys; i++)
    {
	len = nMaxKeyLen+1;
	len2 = nMaxValueLen+1;
	result = RegEnumValue(tkey, i, pszKey, &len, NULL, &dwType, (unsigned char *)pszValue, &len2); 
	if (result != ERROR_SUCCESS)
	{
	    err_printf("RegEnumKeyEx failed, error %d\n", result);
	}
	else
	{
	    //dbg_printf("key = %s, ", pszKey);
	    // Should I check here if key=phrase and not print out the value?
	    switch (dwType)
	    {
	    case REG_SZ:
		//dbg_printf("value = %s\n", pszValue);
		strncat(pszStr, pszKey, length - strlen(pszStr));
		strncat(pszStr, "=", length - strlen(pszStr));
		strncat(pszStr, pszValue, length - strlen(pszStr));
		strncat(pszStr, "\n", length - strlen(pszStr));
		break;
	    case REG_DWORD:
		//dbg_printf("value = %s\n", pszValue);
		strncat(pszStr, pszKey, length - strlen(pszStr));
		strncat(pszStr, "=", length - strlen(pszStr));
		result = *((DWORD*)pszValue);
		_snprintf(pszNum, 10, "%d\n", result);
		strncat(pszStr, pszNum, length - strlen(pszStr));
		break;
	    default:
		err_printf("unhandled registry type: %d\n", dwType);
		break;
	    }
	}
    }

    delete pszKey;
    delete pszValue;

    RegCloseKey(tkey);
}

void WriteMPDRegistry(char *name, char *value)
{
    HKEY tkey;
    DWORD result;

    // Open the root key
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }
    if (RegSetValueEx(tkey, name, 0, REG_SZ, (const unsigned char *)value, strlen(value)+1) != ERROR_SUCCESS)
    {
	if (stricmp(name, "phrase") == 0)
	{
	    err_printf("WriteMPDRegistry failed to write '%s: ***', error %d\n", name, GetLastError());
	}
	else
	{
	    err_printf("WriteMPDRegistry failed to write '%s:%s', error %d\n", name, value, GetLastError());
	}
    }
    else
    {
	/*
	if (stricmp(name, "phrase") == 0)
	{
	    dbg_printf("WriteMPDRegistry: %s = ***\n", name); // don't show the passphrase
	}
	else
	{
	    dbg_printf("WriteMPDRegistry: %s = %s\n", name, value);
	}
	*/
    }
    RegCloseKey(tkey);
}

void DeleteMPDRegistry(char *name)
{
    HKEY tkey;
    DWORD result;

    // Open the root key
    if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &tkey, &result) != ERROR_SUCCESS)
    {
	return;
    }
    // Delete the entry
    if (RegDeleteValue(tkey, name) != ERROR_SUCCESS)
    {
	err_printf("DeleteMPDRegistry failed to delete '%s', error %d\n", name, GetLastError());
    }
    /*
    else
    {
	dbg_printf("DeleteMPDRegistry: %s\n", name);
    }
    */
    RegCloseKey(tkey);
}

void ParseRegistry(bool bSetDefaults)
{
    HKEY tkey;
    DWORD result, len;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH];
    char port[10];
    char str[4096];
    DWORD dwAccess;

    // Set the defaults.
    g_nPort = MPD_DEFAULT_PORT;
    gethostname(g_pszHost, 100);
    strncpy(g_pszLeftHost, g_pszHost, MAX_HOST_LENGTH);
    
    // Open the root key
    dwAccess =  (bSetDefaults) ? KEY_ALL_ACCESS : KEY_READ;
    result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, dwAccess, &tkey);
    if (result != ERROR_SUCCESS)
    {
	if (bSetDefaults)
	{
	    err_printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", result);
	}
	return;
    }
    
    // Read the port
    len = 10;
    result = RegQueryValueEx(tkey, "port", 0, NULL, (unsigned char *)port, &len);
    if (result == ERROR_SUCCESS)
    {
	g_nPort = atoi(port);
    }
    else if (bSetDefaults)
    {
	sprintf(port, "%d", MPD_DEFAULT_PORT);
	RegSetValueEx(tkey, "port", 0, REG_SZ, (const unsigned char *)port, strlen(port)+1);
    }

    // Read the insert point
    len = 100;
    g_pszInsertHost[0] = '\0';
    RegQueryValueEx(tkey, INSERT1, 0, NULL, (unsigned char *)g_pszInsertHost, &len);

    // Read the second insert point
    len = 100;
    g_pszInsertHost2[0] = '\0';
    RegQueryValueEx(tkey, INSERT2, 0, NULL, (unsigned char *)g_pszInsertHost2, &len);

    // Read the temp directory
    len = MAX_PATH;
    result = RegQueryValueEx(tkey, "temp", 0, NULL, (unsigned char *)g_pszTempDir, &len);
    if (result != ERROR_SUCCESS && bSetDefaults)
	RegSetValueEx(tkey, "temp", 0, REG_SZ, (const unsigned char *)"C:\\", strlen("C:\\")+1);

    // Read the logfile option
    len = 100;
    result = RegQueryValueEx(tkey, "RedirectToLogfile", 0, NULL, (unsigned char *)str, &len);
    if (result != ERROR_SUCCESS)
    {
	if (bSetDefaults)
	    RegSetValueEx(tkey, "RedirectToLogfile", 0, REG_SZ, (const unsigned char *)"no", 4);
    }
    else
    {
	if (stricmp(str, "yes") == 0)
	{
	    len = 4096;
	    if (RegQueryValueEx(tkey, "LogFile", 0, NULL, (unsigned char *)str, &len) == ERROR_SUCCESS)
	    {
		SetDbgRedirection(str);
	    }
	}
	else
	{
	    CancelDbgRedirection();
	}
    }

    // Check to see if a passphrase has been set and set it to the default if necessary.
    len = MPD_PASSPHRASE_MAX_LENGTH;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)phrase, &len);
    if (result != ERROR_SUCCESS && bSetDefaults)
	RegSetValueEx(tkey, "phrase", 0, REG_SZ, (const unsigned char *)MPD_DEFAULT_PASSPHRASE, strlen(MPD_DEFAULT_PASSPHRASE)+1);

    len = 100;
    result = RegQueryValueEx(tkey, "SingleUser", 0, NULL, (unsigned char *)str, &len);
    if (result != ERROR_SUCCESS)
    {
	if (bSetDefaults)
	    RegSetValueEx(tkey, "SingleUser", 0, REG_SZ, (const unsigned char *)"no", 4);
	g_bSingleUser = false;
    }
    else
    {
	g_bSingleUser = (stricmp(str, "yes") == 0) ? true : false;
    }

    /*
    // Check to see what mode we are in.  The default is RshMode, not RingMode
    // Check rshmode first
    len = 100;
    result = RegQueryValueEx(tkey, "rshmode", 0, NULL, (unsigned char *)str, &len);
    if (result != ERROR_SUCCESS && bSetDefaults)
    {
	RegSetValueEx(tkey, "rshmode", 0, REG_SZ, (const unsigned char *)"yes", 4);
	g_bRshMode = true;
	// If there are no hosts names set, insert the local host name
	if (RegQueryValueEx(tkey, "hosts", 0, NULL, NULL, &len) != ERROR_SUCCESS)
	{
	    RegSetValueEx(tkey, "hosts", 0, REG_SZ, (const unsigned char *)g_pszHost, strlen(g_pszHost)+1);
	}
    }
    else
    {
	g_bRshMode = (stricmp(str, "yes") == 0) ? true : false;
    }
    // Check ringmode second
    len = 100;
    result = RegQueryValueEx(tkey, "ringmode", 0, NULL, (unsigned char *)str, &len);
    if (result != ERROR_SUCCESS && bSetDefaults)
    {
	RegSetValueEx(tkey, "ringmode", 0, REG_SZ, (const unsigned char *)"no", 4);
	g_bRingMode = false;
    }
    else
    {
	g_bRingMode = (stricmp(str, "yes") == 0) ? true : false;
    }
    */

    RegCloseKey(tkey);

    if (bSetDefaults)
    {
	result = RegOpenKeyEx(HKEY_LOCAL_MACHINE, 
	    MPICHKEY,
	    0, KEY_ALL_ACCESS, &tkey);
	if (result == ERROR_SUCCESS)
	{
	    DWORD job_number = 0;
	    RegSetValueEx(tkey, "Job Number", 0, REG_DWORD, (const unsigned char *)&job_number, sizeof(DWORD));
	    RegCloseKey(tkey);
	}
    }
    //dbg_printf("ParseRegistry: port %d, insert 1 '%s', insert 2 '%s', %s\n", g_nPort, g_pszInsertHost, g_pszInsertHost2, g_bRshMode ? "RshMode" : "RingMode");
    //dbg_printf("ParseRegistry: port %d, insert 1 '%s', insert 2 '%s'\n", g_nPort, g_pszInsertHost, g_pszInsertHost2);
}
