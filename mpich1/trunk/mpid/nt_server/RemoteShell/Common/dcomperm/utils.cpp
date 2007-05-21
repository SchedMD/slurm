/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    utils.cpp

Abstract:

    Miscellaneous utility functions

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
GetCurrentUserSID (
    PSID *Sid
    )
{
    TOKEN_USER  *tokenUser=NULL;
    HANDLE      tokenHandle;
    DWORD       tokenSize;
    DWORD       sidLength;

    if (OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &tokenHandle))
    {
        GetTokenInformation (tokenHandle,
                             TokenUser,
                             tokenUser,
                             0,
                             &tokenSize);

        tokenUser = (TOKEN_USER *) malloc (tokenSize);

        if (GetTokenInformation (tokenHandle,
                                 TokenUser,
                                 tokenUser,
                                 tokenSize,
                                 &tokenSize))
        {
            sidLength = GetLengthSid (tokenUser->User.Sid);
            *Sid = (PSID) malloc (sidLength);

            memcpy (*Sid, tokenUser->User.Sid, sidLength);
            CloseHandle (tokenHandle);
        } else
        {
            free (tokenUser);
            return GetLastError();
        }
    } else
    {
        free (tokenUser);
        return GetLastError();
    }

    free (tokenUser);
    return ERROR_SUCCESS;
}

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

LPTSTR
SystemMessage (
    LPTSTR Buffer,
    HRESULT hr
    )
{
    LPTSTR   message;

    FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                   FORMAT_MESSAGE_FROM_SYSTEM,
                   NULL,
                   hr,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   (LPTSTR) &message,
                   0,
                   NULL);

    wsprintf (Buffer, TEXT("%s(%lx)\n"), message, hr);
    
    LocalFree (message);
    return Buffer;
}
