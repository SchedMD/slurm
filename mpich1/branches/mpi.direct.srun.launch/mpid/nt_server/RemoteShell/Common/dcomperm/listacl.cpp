/*++

DCOM Permission Configuration Sample
Copyright (c) 1996, Microsoft Corporation. All rights reserved.

Module Name:

    listacl.cpp

Abstract:

    Code to list ACL information

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

TCHAR **g_aPrincipal = NULL;
bool *g_bPermit=NULL;

void
ListACL (
    PACL Acl
    )
{
    ACL_SIZE_INFORMATION     aclSizeInfo;
    ACL_REVISION_INFORMATION aclRevInfo;
    ULONG                    i;
    LPVOID                   ace;
    ACE_HEADER               *aceHeader;
    ACCESS_ALLOWED_ACE       *paaace;
    ACCESS_DENIED_ACE        *padace;
    TCHAR                    domainName [256];
    TCHAR                    userName [256];
    DWORD                    nameLength;
    SID_NAME_USE             snu;

	g_aPrincipal = NULL;

    if (!GetAclInformation (Acl,
                            &aclSizeInfo,
                            sizeof (ACL_SIZE_INFORMATION),
                            AclSizeInformation))
    {
        _tprintf (TEXT("Could not get AclSizeInformation"));
        return;
    }

    if (!GetAclInformation (Acl,
                            &aclRevInfo,
                            sizeof (ACL_REVISION_INFORMATION),
                            AclRevisionInformation))
    {
        _tprintf (TEXT("Could not get AclRevisionInformation"));
        return;
    }

	g_aPrincipal = new TCHAR*[aclSizeInfo.AceCount+1];
	g_aPrincipal[aclSizeInfo.AceCount] = NULL;
	g_bPermit = new bool[aclSizeInfo.AceCount];
	TCHAR tBuffer[256];
	DWORD length;

    for (i = 0; i < aclSizeInfo.AceCount; i++)
    {
        if (!GetAce (Acl, i, &ace))
            return;

        aceHeader = (ACE_HEADER *) ace;

        if (aceHeader->AceType == ACCESS_ALLOWED_ACE_TYPE)
        {
            paaace = (ACCESS_ALLOWED_ACE *) ace;
            nameLength = 255;
            LookupAccountSid (NULL,
                              &paaace->SidStart,
                              userName,
                              &nameLength,
                              domainName,
                              &nameLength,
                              &snu);

            //_tprintf (TEXT("Access permitted to %s\\%s.\n"), domainName, userName);
			//_stprintf (tBuffer, TEXT("%s\\%s"), domainName, userName);
			_stprintf (tBuffer, TEXT("%s"), userName);
			length = _tcslen(tBuffer)+1;
			g_aPrincipal[i] = new TCHAR[length];
			_tcscpy(g_aPrincipal[i], tBuffer);
			g_bPermit[i] = true;
        } 
		else
        if (aceHeader->AceType == ACCESS_DENIED_ACE_TYPE)
        {
            padace = (ACCESS_DENIED_ACE *) ace;
            nameLength = 255;
            LookupAccountSid (NULL,
                              &padace->SidStart,
                              userName,
                              &nameLength,
                              domainName,
                              &nameLength,
                              &snu);

            //_tprintf (TEXT("Access denied to %s\\%s.\n"), domainName, userName);
			//_stprintf (tBuffer, TEXT("%s\\%s"), domainName, userName);
			_stprintf (tBuffer, TEXT("%s"), userName);
			length = _tcslen(tBuffer)+1;
			g_aPrincipal[i] = new TCHAR[length];
			_tcscpy(g_aPrincipal[i], tBuffer);
			g_bPermit[i] = false;
        }
   }
}
