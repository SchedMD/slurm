/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    srvcmgmt.cpp

Abstract:

    Routines to manage RunAs and Service settings for DCOM servers

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

DWORD GetRunAsPassword (
    LPTSTR AppID,
    LPTSTR Password
    )
{
    LSA_OBJECT_ATTRIBUTES objectAttributes;
    HANDLE                policyHandle = NULL;
    LSA_UNICODE_STRING    lsaKeyString;
    PLSA_UNICODE_STRING   lsaPasswordString;
    WCHAR                 key [4 + GUIDSTR_MAX + 1];
    WCHAR                 wideAppID [GUIDSTR_MAX + 1];
    ULONG                 returnValue;

#ifndef UNICODE
    STR2UNI (wideAppID, AppID);
#else
    lstrcpy (wideAppID, AppID);
#endif

    wcscpy (key, L"SCM:");
    wcscat (key, wideAppID);

    lsaKeyString.Length = (USHORT) ((wcslen (key) + 1) * sizeof (WCHAR));
    lsaKeyString.MaximumLength = (GUIDSTR_MAX + 5) * sizeof (WCHAR);
    lsaKeyString.Buffer = key;

    //
    // Open the local security policy
    //

    memset (&objectAttributes, 0x00, sizeof (LSA_OBJECT_ATTRIBUTES));
    objectAttributes.Length = sizeof (LSA_OBJECT_ATTRIBUTES);

    returnValue = LsaOpenPolicy (NULL,
                                 &objectAttributes,
                                 POLICY_GET_PRIVATE_INFORMATION,
                                 &policyHandle);

    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    //
    // Read the user's password
    //

    returnValue = LsaRetrievePrivateData (policyHandle,
                                          &lsaKeyString,
                                          &lsaPasswordString);

    if (returnValue != ERROR_SUCCESS)
    {
        LsaClose (policyHandle);
        return returnValue;
    }

    LsaClose (policyHandle);

#ifndef UNICODE
    UNI2STR (Password, lsaPasswordString->Buffer);
#else
    wcscpy (Password, lsaPasswordString->Buffer);
#endif

    return ERROR_SUCCESS;
}

DWORD SetRunAsPassword (
    LPTSTR AppID,
    LPTSTR Principal,
    LPTSTR Password
    )
{
    LSA_OBJECT_ATTRIBUTES objectAttributes;
    HANDLE                policyHandle = NULL;
    LSA_UNICODE_STRING    lsaKeyString;
    LSA_UNICODE_STRING    lsaPasswordString;
    WCHAR                 key [4 + GUIDSTR_MAX + 1];
    WCHAR                 wideAppID [GUIDSTR_MAX + 1];
    WCHAR                 widePassword [256];
    DWORD                 returnValue;

#ifndef UNICODE
    STR2UNI (wideAppID, AppID);
    STR2UNI (widePassword, Password);
#else
    wcscpy (wideAppID, AppID);
    wcscpy (widePassword, Password);
#endif

    wcscpy (key, L"SCM:");
    wcscat (key, wideAppID);

    lsaKeyString.Length = (USHORT) ((wcslen (key) + 1) * sizeof (WCHAR));
    lsaKeyString.MaximumLength = (GUIDSTR_MAX + 5) * sizeof (WCHAR);
    lsaKeyString.Buffer = key;

    lsaPasswordString.Length = (USHORT) ((wcslen (widePassword) + 1) * sizeof (WCHAR));
    lsaPasswordString.Buffer = widePassword;
    lsaPasswordString.MaximumLength = lsaPasswordString.Length;

    //
    // Open the local security policy
    //

    memset (&objectAttributes, 0x00, sizeof (LSA_OBJECT_ATTRIBUTES));
    objectAttributes.Length = sizeof (LSA_OBJECT_ATTRIBUTES);

    returnValue = LsaOpenPolicy (NULL,
                                 &objectAttributes,
                                 POLICY_CREATE_SECRET,
                                 &policyHandle);

    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    //
    // Store the user's password
    //

    returnValue = LsaStorePrivateData (policyHandle,
                                       &lsaKeyString,
                                       &lsaPasswordString);

    if (returnValue != ERROR_SUCCESS)
    {
        LsaClose (policyHandle);
        return returnValue;
    }

    LsaClose (policyHandle);

    returnValue = SetAccountRights (Principal, TEXT("SeBatchLogonRight"));
    if (returnValue != ERROR_SUCCESS)
        return returnValue;

    return ERROR_SUCCESS;
}

DWORD
SetAccountRights (
    LPTSTR User,
    LPTSTR Privilege
    )
{
    LSA_HANDLE            policyHandle;
    LSA_OBJECT_ATTRIBUTES objectAttributes;
    PSID                  principalSID;
    LSA_UNICODE_STRING    lsaPrivilegeString;
    WCHAR                 widePrivilege [256];

#ifdef _UNICODE
    lstrcpy (widePrivilege, Privilege);
#else
    STR2UNI (widePrivilege, Privilege);
#endif

    memset (&objectAttributes, 0, sizeof(LSA_OBJECT_ATTRIBUTES));
    if (LsaOpenPolicy (NULL,
                       &objectAttributes,
                       POLICY_CREATE_ACCOUNT | POLICY_LOOKUP_NAMES,
                       &policyHandle) != ERROR_SUCCESS)
    {
        return GetLastError();
    }

    GetPrincipalSID (User, &principalSID);

    lsaPrivilegeString.Length = (USHORT) (wcslen (widePrivilege) * sizeof (WCHAR));
    lsaPrivilegeString.MaximumLength = (USHORT) (lsaPrivilegeString.Length + sizeof (WCHAR));
    lsaPrivilegeString.Buffer = widePrivilege;

    if (LsaAddAccountRights (policyHandle,
                             principalSID,
                             &lsaPrivilegeString,
                             1) != ERROR_SUCCESS)
    {
        free (principalSID);
        LsaClose (policyHandle);
        return GetLastError();
    }

    free (principalSID);
    LsaClose (policyHandle);

    return ERROR_SUCCESS;
}
