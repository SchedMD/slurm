/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    dcomperm.cpp

Abstract:

    Main module for DCOM Permission Configuration Sample

Author:

    Michael Nelson

Environment:

    Windows NT

--*/

#include "stdafx.h"
#include <windows.h>
#include <stdio.h>
#include <conio.h>
#include <tchar.h>
#include "ntsecapi.h"
#include "dcomperm.h"

/*
void
ShowUsage (
    LPTSTR ErrorString
    )
{
    _tprintf (TEXT("%s\n"), ErrorString);
    _tprintf (TEXT("Syntax: dcomperm <option> [...]\n\n"));

    _tprintf (TEXT("Options:\n"));

    _tprintf (TEXT("   -da <\"set\" or \"remove\"> <Principal Name> [\"permit\" or \"deny\"]\n"));
    _tprintf (TEXT("   -da list\n"));
    _tprintf (TEXT("       Modify or list the default access permission list\n\n"));

    _tprintf (TEXT("   -dl <\"set\" or \"remove\"> <Principal Name> [\"permit\" or \"deny\"]\n"));
    _tprintf (TEXT("   -dl list\n"));
    _tprintf (TEXT("       Modify or list the default launch permission list\n\n"));

    _tprintf (TEXT("   -aa <AppID> <\"set\" or \"remove\"> <Principal Name> [\"permit\" or \"deny\"]\n"));
    _tprintf (TEXT("   -aa <AppID> default\n"));
    _tprintf (TEXT("   -aa <AppID> list\n"));
    _tprintf (TEXT("       Modify or list the access permission list for a specific AppID\n\n"));

    _tprintf (TEXT("   -al <AppID> <\"set\" or \"remove\"> <Principal Name> [\"permit\" or \"deny\"]\n"));
    _tprintf (TEXT("   -al <AppID> default\n"));
    _tprintf (TEXT("   -al <AppID> list\n"));
    _tprintf (TEXT("       Modify or list the launch permission list for a specific AppID\n\n"));

    _tprintf (TEXT("Press any key to continue. . ."));
    _getch();
    _tprintf (TEXT("\r                               \r"));

    _tprintf (TEXT("   -runas <AppID> <Principal Name> <Password>\n"));
    _tprintf (TEXT("   -runas <AppID> \"Interactive User\"\n"));
    _tprintf (TEXT("       Set the RunAs information for a specific AppID\n\n"));

    _tprintf (TEXT("Examples:\n"));
    _tprintf (TEXT("   dcomperm -da set redmond\\t-miken permit\n"));
    _tprintf (TEXT("   dcomperm -dl set redmond\\jdoe deny\n"));
    _tprintf (TEXT("   dcomperm -aa {12345678-1234-1234-1234-00aa00bbf7c7} list\n"));
    _tprintf (TEXT("   dcomperm -al {12345678-1234-1234-1234-00aa00bbf7c7} remove redmond\\t-miken\n"));
    _tprintf (TEXT("   dcomperm -runas {12345678-1234-1234-1234-00aa00bbf7c7} redmond\\jdoe password\n"));

    exit (0);
}
//*/

//*
void
Error (
    LPTSTR ErrorMessage,
    DWORD ErrorCode
    )
{
    TCHAR messageBuffer [255];

    _tprintf (TEXT("%s\n%s"), ErrorMessage, SystemMessage (messageBuffer, ErrorCode));
    //exit (0);
}
//*/

/*
void
HandleDAOption (
    int argc,
    TCHAR **argv
    )
{
    DWORD returnValue;

    if (argc < 3)
        ShowUsage (TEXT("Invalid number of arguments."));

    if (_tcscmp (_tcsupr (argv [2]), TEXT("LIST")) == 0)
    {
        _tprintf (TEXT("Default access permission list:\n\n"));
        ListDefaultAccessACL();
        return;
    }

    if (argc < 4)
        ShowUsage (TEXT("Invalid number of arguments."));

    if (_tcscmp (_tcsupr (argv [2]), TEXT("SET")) == 0)
    {
        if (argc < 5)
            ShowUsage (TEXT("Invalid number of arguments."));

        if (_tcscmp (_tcsupr (argv [4]), TEXT("PERMIT")) == 0)
            returnValue = ChangeDefaultAccessACL (argv [3], TRUE, TRUE); else

        if (_tcscmp (_tcsupr (argv [4]), TEXT("DENY")) == 0)
            returnValue = ChangeDefaultAccessACL (argv [3], TRUE, FALSE); else
        {
            ShowUsage (TEXT("You can only set a user's permissions to \"permit\" or \"deny\".\n\n"));
        }

        if (returnValue != ERROR_SUCCESS)
            Error (TEXT("ERROR: Cannot add user to default access ACL."), returnValue);
    } else
    if (_tcscmp (_tcsupr (argv [2]), TEXT("REMOVE")) == 0)
    {
        returnValue = ChangeDefaultAccessACL (argv[3], FALSE, FALSE);

        if (returnValue != ERROR_SUCCESS)
            Error (TEXT("ERROR: Cannot remove user from default access ACL."), returnValue);
    } else
        ShowUsage (TEXT("You can only \"set\" or \"remove\" a user."));
}
//*/

/*
void
HandleDLOption (
    int argc,
    TCHAR **argv
    )
{
    DWORD returnValue;

    if (argc < 3)
        ShowUsage (TEXT("Invalid number of arguments."));

    if (_tcscmp (_tcsupr (argv [2]), TEXT("LIST")) == 0)
    {
        _tprintf (TEXT("Default launch permission list:\n\n"));
        ListDefaultLaunchACL();
        return;
    }

    if (argc < 4)
        ShowUsage (TEXT("Invalid number of arguments."));

    if (_tcscmp (_tcsupr (argv [2]), TEXT("SET")) == 0)
    {
        if (argc < 5)
            ShowUsage (TEXT("Invalid number of arguments."));

        if (_tcscmp (_tcsupr (argv [4]), TEXT("PERMIT")) == 0)
            returnValue = ChangeDefaultLaunchACL (argv [3], TRUE, TRUE); else

        if (_tcscmp (_tcsupr (argv [4]), TEXT("DENY")) == 0)
            returnValue = ChangeDefaultLaunchACL (argv [3], TRUE, FALSE); else
        {
            ShowUsage (TEXT("You can only set a user's permissions to \"permit\" or \"deny\".\n\n"));
        }

        if (returnValue != ERROR_SUCCESS)
            Error (TEXT("ERROR: Cannot add user to default launch ACL."), returnValue);
    } else
    if (_tcscmp (_tcsupr (argv [3]), TEXT("REMOVE")) == 0)
    {
        returnValue = ChangeDefaultLaunchACL (argv[3], FALSE, FALSE);

        if (returnValue != ERROR_SUCCESS)
            Error (TEXT("ERROR: Cannot remove user from default launch ACL."), returnValue);
    } else
        ShowUsage (TEXT("You can only \"set\" or \"remove\" a user."));
}
//*/

DWORD
HandleAAOption (
	TCHAR *IIDString,
	TCHAR *Principal,
	bool bSet,
	bool bPermit
    )
{
    DWORD returnValue=0;
    //HKEY  registryKey;
    //TCHAR appid [256];
    //TCHAR keyName [256];

	/*
    if (_tcscmp (_tcsupr (argv[3]), TEXT("LIST")) == 0)
    {
        if (argc < 4)
            ShowUsage (TEXT("Invalid number of arguments."));

        _tprintf (TEXT("Access permission list for AppID %s:\n\n"), argv[2]);
        ListAppIDAccessACL (argv[2]);
        return;
    }
	//*/

	/*
    if (_tcscmp (_tcsupr (argv[3]), TEXT("DEFAULT")) == 0)
    {
        if (argv [2][0] == '{')
            wsprintf (appid, TEXT("%s"), argv [2]); else
            wsprintf (appid, TEXT("{%s}"), argv [2]);

        wsprintf (keyName, TEXT("APPID\\%s"), appid);

        returnValue = RegOpenKeyEx (HKEY_CLASSES_ROOT, keyName, 0, KEY_ALL_ACCESS, &registryKey);
        if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND)
            Error (TEXT("ERROR: Cannot open AppID registry key."), returnValue);

        returnValue = RegDeleteValue (registryKey, TEXT("AccessPermission"));
        if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND)
            Error (TEXT("ERROR: Cannot delete AccessPermission value."), returnValue);

        RegCloseKey (registryKey);
        return;
    }
	//*/

	if (bSet)
    {
		if (bPermit)
            returnValue = ChangeAppIDAccessACL (IIDString, Principal, TRUE, TRUE);
		else
            returnValue = ChangeAppIDAccessACL (IIDString, Principal, TRUE, FALSE);

        //if (returnValue != ERROR_SUCCESS)
          //  Error (TEXT("ERROR: Cannot add user to application access ACL."), returnValue);
    } 
	else
    {
        returnValue = ChangeAppIDAccessACL (IIDString, Principal, FALSE, FALSE);

        //if (returnValue != ERROR_SUCCESS)
          //  Error (TEXT("ERROR: Cannot remove user from application access ACL."), returnValue);
    }

	return returnValue;
}

DWORD
HandleALOption (
	TCHAR *IIDString,
	TCHAR *Principal,
	bool bSet,
	bool bPermit
    )
{
    DWORD returnValue=0;
    //HKEY  registryKey;
    //TCHAR appid [256];
    //TCHAR keyName [256];

	/*
    if (_tcscmp (_tcsupr (argv[3]), TEXT("LIST")) == 0)
    {
        _tprintf (TEXT("Launch permission list for AppID %s:\n\n"), argv[2]);
        ListAppIDLaunchACL (argv[2]);
        return;
    }
	//*/

	/*
    if (_tcscmp (_tcsupr (argv[3]), TEXT("DEFAULT")) == 0)
    {
        if (argv [2][0] == '{')
            wsprintf (appid, TEXT("%s"), argv [2]); else
            wsprintf (appid, TEXT("{%s}"), argv [2]);

        wsprintf (keyName, TEXT("APPID\\%s"), appid);

        returnValue = RegOpenKeyEx (HKEY_CLASSES_ROOT, keyName, 0, KEY_ALL_ACCESS, &registryKey);
        if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND)
            Error (TEXT("ERROR: Cannot open AppID registry key."), returnValue);

        returnValue = RegDeleteValue (registryKey, TEXT("LaunchPermission"));
        if (returnValue != ERROR_SUCCESS && returnValue != ERROR_FILE_NOT_FOUND)
            Error (TEXT("ERROR: Cannot delete LaunchPermission value."), returnValue);

        RegCloseKey (registryKey);
        return;
    }
	//*/

	if (bSet)
    {
		if (bPermit)
            returnValue = ChangeAppIDLaunchACL (IIDString, Principal, TRUE, TRUE);
		else
            returnValue = ChangeAppIDLaunchACL (IIDString, Principal, TRUE, FALSE);

        //if (returnValue != ERROR_SUCCESS)
          //  Error (TEXT("ERROR: Cannot add user to application launch ACL."), returnValue);
    } 
	else
    {
        returnValue = ChangeAppIDLaunchACL (IIDString, Principal, FALSE, FALSE);

        //if (returnValue != ERROR_SUCCESS)
          //  Error (TEXT("ERROR: Cannot remove user from application launch ACL."), returnValue);
    }

	return returnValue;
}

DWORD
HandleRunAsOption (
	TCHAR *IIDString,
	TCHAR *Account,
	TCHAR *Password
    )
{
    DWORD returnValue=0;
    HKEY  registryKey;
    TCHAR appid [256];
    TCHAR keyName [256];

    if (IIDString[0] == '{')
        wsprintf (appid, TEXT("%s"), IIDString); 
	else
        wsprintf (appid, TEXT("{%s}"), IIDString);

    wsprintf (keyName, TEXT("APPID\\%s"), appid);

    returnValue = RegOpenKeyEx (HKEY_CLASSES_ROOT, keyName, 0, KEY_ALL_ACCESS, &registryKey);
    if (returnValue != ERROR_SUCCESS)
		return returnValue;
        //Error (TEXT("ERROR: Cannot open AppID registry key."), returnValue);

    returnValue = RegSetValueEx (registryKey, TEXT("RunAs"), 0, REG_SZ, (LPBYTE) Account, (_tcslen(Account)+1) * sizeof (TCHAR));
    if (returnValue != ERROR_SUCCESS)
		return returnValue;
        //Error (TEXT("ERROR: Cannot set RunAs registry value."), returnValue);

    RegCloseKey (registryKey);

    if ((_tcscmp (_tcsupr (Account), TEXT("INTERACTIVE USER")) != 0) &&
		(_tcscmp (_tcsupr (Account), TEXT("INTERACTIVE")) != 0))
    {
        returnValue = SetRunAsPassword (IIDString, Account, Password);
        if (returnValue != ERROR_SUCCESS)
            Error (TEXT("ERROR: Cannot set RunAs password."), returnValue);
    }

	return returnValue;
}

// Function name	: DCOMGetACLPrincipals
// Description	    : This function is not thread safe !!!
// Return type		: bool 
// Argument         : TCHAR *IIDString
// Argument         : bool bAccess
// Argument         : TCHAR **aPrincipal
bool DCOMGetACLPrincipals(
	TCHAR *IIDString, 
	bool bAccess, 
	TCHAR **&aPrincipal,
	bool *&bPermit)
{
	DWORD result=0;
	if (bAccess)
	{
		result = ListAppIDAccessACL(IIDString);
	}
	else
	{
        result = ListAppIDLaunchACL(IIDString);
	}

	if (result)
		return false;

	aPrincipal = g_aPrincipal;
	bPermit = g_bPermit;
	return true;
}

DWORD DCOMPermissions(
	TCHAR *IIDString,
	bool bAccess,
	TCHAR *Principal,
	bool bSet,
	bool bPermit)
{
	DWORD ret_val = 0;

	if (bAccess)
	{
		ret_val = HandleAAOption(IIDString, Principal, bSet, bPermit);
	}
	else
	{
		ret_val = HandleALOption(IIDString, Principal, bSet, bPermit);
	}

	return ret_val;
}

DWORD DCOMSetRunAs(
	TCHAR *IIDString,
	TCHAR *Account,
	TCHAR *Password)
{
	DWORD ret_val = 0;

	ret_val = HandleRunAsOption(IIDString, Account, Password);

	return ret_val;
}

/*
extern "C" void
_tmain (
    int argc,
    TCHAR **argv
    )
{
    if (argc < 2)
        ShowUsage (TEXT("No option specified."));

    //if (_tcscmp (_tcsupr (argv [1]), TEXT("-DA")) == 0)
      //  HandleDAOption (argc, argv); else

    //if (_tcscmp (_tcsupr (argv [1]), TEXT("-DL")) == 0)
      //  HandleDLOption (argc, argv); else

    if (_tcscmp (_tcsupr (argv [1]), TEXT("-AA")) == 0)
        HandleAAOption (argc, argv);

    if (_tcscmp (_tcsupr (argv [1]), TEXT("-AL")) == 0)
        HandleALOption (argc, argv);

    if (_tcscmp (_tcsupr (argv [1]), TEXT("-RUNAS")) == 0)
        HandleRunAsOption (argc, argv);
}
//*/