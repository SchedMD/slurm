#include "privileges.h"
#include <ntsecapi.h>

#ifndef STR2UNI

#define STR2UNI(unistr, regstr) \
        mbstowcs (unistr, regstr, strlen (regstr)+1);

#define UNI2STR(regstr, unistr) \
        wcstombs (regstr, unistr, wcslen (unistr)+1);

#endif

DWORD
GetPrincipalSID (
    LPTSTR Principal,
    PSID *Sid
    )
{
    DWORD        sidSize;
    TCHAR        refDomain [256];
    DWORD        refDomainSize;
    DWORD        returnValue;
    SID_NAME_USE snu;

    sidSize = 0;
    refDomainSize = 255;

    LookupAccountName (NULL,
                       Principal,
                       *Sid,
                       &sidSize,
                       refDomain,
                       &refDomainSize,
                       &snu);

    returnValue = GetLastError();
    if (returnValue != ERROR_INSUFFICIENT_BUFFER)
        return returnValue;

    *Sid = (PSID) malloc (sidSize);
    refDomainSize = 255;

    if (!LookupAccountName (NULL,
                            Principal,
                            *Sid,
                            &sidSize,
                            refDomain,
                            &refDomainSize,
                            &snu))
    {
        return GetLastError();
    }

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
    DWORD                 returnValue;

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

    returnValue = GetPrincipalSID (User, &principalSID);
    if (returnValue != ERROR_SUCCESS)
	return returnValue;

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
