/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    wrappers.cpp

Abstract:

    Wrappers for low-level security and registry functions

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

DWORD
ListDefaultAccessACL()
{
    return ListNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultAccessPermission"));
}

DWORD
ListDefaultLaunchACL()
{
    return ListNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultLaunchPermission"));
}

DWORD
ListAppIDAccessACL (
    LPTSTR AppID
    )
{
    TCHAR   keyName [256];

    if (AppID [0] == '{')
        wsprintf (keyName, TEXT("APPID\\%s"), AppID); else
        wsprintf (keyName, TEXT("APPID\\{%s}"), AppID);

    return ListNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("AccessPermission"));
}

DWORD
ListAppIDLaunchACL (
    LPTSTR AppID
    )
{
    TCHAR   keyName [256];

    if (AppID [0] == '{')
        wsprintf (keyName, TEXT("APPID\\%s"), AppID); else
        wsprintf (keyName, TEXT("APPID\\{%s}"), AppID);

    return ListNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("LaunchPermission"));
}

DWORD
ChangeDefaultAccessACL (
    LPTSTR Principal,
    BOOL SetPrincipal,
    BOOL Permit
    )
{
    if (SetPrincipal)
    {
        RemovePrincipalFromNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultAccessPermission"), Principal);
        return AddPrincipalToNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultAccessPermission"), Principal, Permit);
    } else
        return RemovePrincipalFromNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultAccessPermission"), Principal);
}

DWORD
ChangeDefaultLaunchACL (
    LPTSTR Principal,
    BOOL SetPrincipal,
    BOOL Permit
    )
{
    if (SetPrincipal)
    {
        RemovePrincipalFromNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultLaunchPermission"), Principal);
        return AddPrincipalToNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultLaunchPermission"), Principal, Permit);
    } else
        return RemovePrincipalFromNamedValueSD (HKEY_LOCAL_MACHINE, TEXT("Software\\Microsoft\\Ole"), TEXT("DefaultLaunchPermission"), Principal);
}

DWORD
ChangeAppIDAccessACL (
    LPTSTR AppID,
    LPTSTR Principal,
    BOOL SetPrincipal,
    BOOL Permit
    )
{
    TCHAR   keyName [256];

    if (AppID [0] == '{')
        wsprintf (keyName, TEXT("APPID\\%s"), AppID); else
        wsprintf (keyName, TEXT("APPID\\{%s}"), AppID);

    if (SetPrincipal)
    {
        RemovePrincipalFromNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("AccessPermission"), Principal);
        return AddPrincipalToNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("AccessPermission"), Principal, Permit);
    } else
        return RemovePrincipalFromNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("AccessPermission"), Principal);
}

DWORD
ChangeAppIDLaunchACL (
    LPTSTR AppID,
    LPTSTR Principal,
    BOOL SetPrincipal,
    BOOL Permit
    )
{
    TCHAR   keyName [256];

    if (AppID [0] == '{')
        wsprintf (keyName, TEXT("APPID\\%s"), AppID); else
        wsprintf (keyName, TEXT("APPID\\{%s}"), AppID);

    if (SetPrincipal)
    {
        RemovePrincipalFromNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("LaunchPermission"), Principal);
        return AddPrincipalToNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("LaunchPermission"), Principal, Permit);
    } else
        return RemovePrincipalFromNamedValueSD (HKEY_CLASSES_ROOT, keyName, TEXT("LaunchPermission"), Principal);
}
