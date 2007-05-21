#include "stdafx.h"
#include "AccessDesktop.h"
#include "..\Common\RemoteShellLog.h"

#define WINSTA_ALL (WINSTA_ACCESSCLIPBOARD | WINSTA_ACCESSGLOBALATOMS | \
WINSTA_CREATEDESKTOP   | WINSTA_ENUMDESKTOPS   | WINSTA_ENUMERATE	  | \
WINSTA_EXITWINDOWS	   | WINSTA_READATTRIBUTES | WINSTA_READSCREEN	  | \
WINSTA_WRITEATTRIBUTES | DELETE | READ_CONTROL | WRITE_DAC | WRITE_OWNER)

#define DESKTOP_ALL (DESKTOP_CREATEMENU 	 | DESKTOP_CREATEWINDOW  |\
DESKTOP_ENUMERATE		| DESKTOP_HOOKCONTROL	|\
DESKTOP_JOURNALPLAYBACK | DESKTOP_JOURNALRECORD |\
DESKTOP_READOBJECTS 	| DESKTOP_SWITCHDESKTOP |\
DESKTOP_WRITEOBJECTS	| DELETE				|\
READ_CONTROL			| WRITE_DAC 			|\
WRITE_OWNER)

#define GENERIC_ACCESS (GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL)

#include <windows.h>
#include <stdio.h>

BOOL ObtainSid(
			   HANDLE hToken,			// handle to an process access token
			   PSID   *psid 			// ptr to the buffer of the logon sid
			   );

BOOL AddTheAceWindowStation(
							HWINSTA hwinsta,		 // handle to a windowstation
							PSID	psid			 // logon sid of the process
							);

BOOL AddTheAceDesktop(
					  HDESK hdesk,			   // handle to a desktop
					  PSID	psid			   // logon sid of the process
					  );

RTN_TYPE MyGrantAccessToDesktop(HANDLE hToken)
{
	HDESK				hdesk			= NULL;
	HWINSTA 			hwinsta			= NULL;
	PSID				psid			= NULL;
	HWINSTA				hwinstasaved	= NULL;

	hwinstasaved = GetProcessWindowStation();
	if (hwinstasaved == NULL)
		return RTN_ERROR;

	//
	// obtain a handle to the interactive windowstation
	//
	hwinsta = OpenWindowStation(
		"winsta0",
		FALSE,
		READ_CONTROL | WRITE_DAC
		);
	if (hwinsta == NULL)
	{
		LogMsg(TEXT("OpenWindowStation failed: %d"), GetLastError());
		return RTN_ERROR;
	}
	
	//
	// set the windowstation to winsta0 so that you obtain the
	// correct default desktop
	//
	if (!SetProcessWindowStation(hwinsta))
	{
		LogMsg(TEXT("SetProcessWindowStation failed: %d"), GetLastError());
		CloseWindowStation(hwinsta);
		return RTN_ERROR;
	}
	
	//
	// obtain a handle to the "default" desktop
	//
	hdesk = OpenDesktop(
		"default",
		0,
		FALSE,
		READ_CONTROL | WRITE_DAC |
		DESKTOP_WRITEOBJECTS | DESKTOP_READOBJECTS
		);
	if (hdesk == NULL)
	{
		LogMsg(TEXT("OpenDesktop failed: %d"), GetLastError());
		SetProcessWindowStation(hwinstasaved);
		CloseWindowStation(hwinsta);
		return RTN_ERROR;
	}
	
	//
	// obtain the logon sid of the user fester
	//
	if (!ObtainSid(hToken, &psid))
	{
		LogMsg(TEXT("ObtainSid failed: %d"), GetLastError());
		SetProcessWindowStation(hwinstasaved);
		CloseDesktop(hdesk);
		CloseWindowStation(hwinsta);
		return RTN_ERROR;
	}
	
	//
	// add the user to interactive windowstation
	//
	if (!AddTheAceWindowStation(hwinsta, psid))
	{
		LogMsg(TEXT("AddTheAceWindowStation failed: %d"), GetLastError());
		SetProcessWindowStation(hwinstasaved);
		CloseDesktop(hdesk);
		CloseWindowStation(hwinsta);
		return RTN_ERROR;
	}
	
	//
	// add user to "default" desktop
	//
	if (!AddTheAceDesktop(hdesk, psid))
	{
		LogMsg(TEXT("AddTheAceDesktop failed: %d"), GetLastError());
		SetProcessWindowStation(hwinstasaved);
		CloseDesktop(hdesk);
		CloseWindowStation(hwinsta);
		return RTN_ERROR;
	}
	
	//
	// free the buffer for the logon sid
	//
	delete psid;
	
	SetProcessWindowStation(hwinstasaved);

	//
	// close the handles to the interactive windowstation and desktop
	//
	CloseWindowStation(hwinsta);
	
	CloseDesktop(hdesk);
	
	return RTN_OK;
}

BOOL ObtainSid(HANDLE hToken, PSID *psid)
{
	DWORD					dwIndex;
	DWORD					dwLength = 0;
	TOKEN_INFORMATION_CLASS tic 	 = TokenGroups;
	PTOKEN_GROUPS			ptg 	 = NULL;
	   
	//
	// determine the size of the buffer
	//
	if (!GetTokenInformation(
		hToken,
		tic,
		(LPVOID)ptg,
		0,
		&dwLength
		))
	{
		if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
		{
			ptg = (PTOKEN_GROUPS)new unsigned char [dwLength];
			if (ptg == NULL)
				return FALSE;
		}
		else
			return FALSE;
	}
	
	//
	// obtain the groups the access token belongs to
	//
	if (!GetTokenInformation(
		hToken,
		tic,
		(LPVOID)ptg,
		dwLength,
		&dwLength
		))
	{
		delete ptg;
		return FALSE;
	}
	
	
	//
	// determine which group is the logon sid
	//
	for (dwIndex = 0; dwIndex < ptg->GroupCount; dwIndex++)
	{
		if ((ptg->Groups[dwIndex].Attributes & SE_GROUP_LOGON_ID)
			==	SE_GROUP_LOGON_ID)
		{
			//
			// determine the length of the sid
			//
			dwLength = GetLengthSid(ptg->Groups[dwIndex].Sid);
			
			//
			// allocate a buffer for the logon sid
			//
			*psid = (PSID)new unsigned char [dwLength];
			if (*psid == NULL)
			{
				delete ptg;
				return FALSE;
			}
			
			//
			// obtain a copy of the logon sid
			//
			if (!CopySid(dwLength, *psid, ptg->Groups[dwIndex].Sid))
			{
				delete ptg;
				delete *psid;
				return FALSE;
			}
			
			//
			// break out of the loop since the logon sid has been
			// found
			//
			break;
		}
	}
	
	//
	// free the buffer for the token group
	//
	if (ptg != NULL)
		delete ptg;
	   
	return TRUE;
}

BOOL AddTheAceWindowStation(HWINSTA hwinsta, PSID psid)
{
	ACCESS_ALLOWED_ACE	*pace	  = NULL;
	ACL_SIZE_INFORMATION aclSizeInfo;
	BOOL				bDaclExist;
	BOOL				bDaclPresent;
	DWORD				dwNewAclSize;
	DWORD				dwSidSize = 0;
	DWORD				dwSdSizeNeeded;
	PACL				pacl;
	PACL				pNewAcl   = NULL;
	PSECURITY_DESCRIPTOR psd	  = NULL;
	PSECURITY_DESCRIPTOR psdNew   = NULL;
	PVOID				pTempAce  = NULL;
	SECURITY_INFORMATION si 	  = DACL_SECURITY_INFORMATION;
	unsigned int		i;
	   
	
	try {
		//
		// obtain the dacl for the windowstation
		//
		if (!GetUserObjectSecurity(
			hwinsta,
			&si,
			psd,
			dwSidSize,
			&dwSdSizeNeeded
			))
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				psd = (PSECURITY_DESCRIPTOR)new unsigned char [dwSdSizeNeeded];
				if (psd == NULL)
					return FALSE;
				
				psdNew = (PSECURITY_DESCRIPTOR)new unsigned char [dwSdSizeNeeded];
				if (psdNew == NULL)
					throw 1;
				
				dwSidSize = dwSdSizeNeeded;
				
				if (!GetUserObjectSecurity( hwinsta, &si, psd, dwSidSize, &dwSdSizeNeeded ))
				{
					LogMsg(TEXT("GUOS1 failed: %d"), GetLastError());
					throw 2;
				}
			}
			else
			{
				LogMsg(TEXT("GUOS2 failed: %d"), GetLastError());
				return FALSE;
			}
		}
		
		//
		// create a new dacl
		//
		if (!InitializeSecurityDescriptor( psdNew, SECURITY_DESCRIPTOR_REVISION ))
		{
			LogMsg(TEXT("ISD failed: %d"), GetLastError());
			throw 3;
		}

		//
		// get dacl from the security descriptor
		//
		if (!GetSecurityDescriptorDacl(
			psd,
			&bDaclPresent,
			&pacl,
			&bDaclExist
			))
		{
			LogMsg(TEXT("GSDDacl failed: %d"), GetLastError());
			throw 4;
		}
		
		//
		// initialize
		//
		ZeroMemory(&aclSizeInfo, sizeof(ACL_SIZE_INFORMATION));
		aclSizeInfo.AclBytesInUse = sizeof(ACL);
		
		//
		// call only if the dacl is not NULL
		//
		if (pacl != NULL)
		{
			// get the file ACL size info
			if (!GetAclInformation(
				pacl,
				(LPVOID)&aclSizeInfo,
				sizeof(ACL_SIZE_INFORMATION),
				AclSizeInformation
				))
			{
				LogMsg(TEXT("GAI failed: %d"), GetLastError());
				throw 5;
			}
		}
		
		//
		// compute the size of the new acl
		//
		dwNewAclSize = aclSizeInfo.AclBytesInUse + (2 *
			sizeof(ACCESS_ALLOWED_ACE)) + (2 * GetLengthSid(psid)) - (2 *
			sizeof(DWORD));
		
		//
		// allocate memory for the new acl
		//
		pNewAcl = (PACL)new unsigned char [dwNewAclSize];
		if (pNewAcl == NULL)
			throw 6;
		
		//
		// initialize the new dacl
		//
		if (!InitializeAcl(pNewAcl, dwNewAclSize, ACL_REVISION))
		{
			LogMsg(TEXT("IAcl failed: %d"), GetLastError());
			throw 7;
		}
		
		//
		// if DACL is present, copy it to a new DACL
		//
		if (bDaclPresent) // only copy if DACL was present
		{
			// copy the ACEs to our new ACL
			if (aclSizeInfo.AceCount)
			{
				for (i = 0; i < aclSizeInfo.AceCount; i++)
				{
					// get an ACE
					if (!GetAce(pacl, i, &pTempAce))
					{
						LogMsg(TEXT("GetAce failed: %d"), GetLastError());
						throw 8;
					}
					
					// add the ACE to the new ACL
					if (!AddAce(
						pNewAcl,
						ACL_REVISION,
						MAXDWORD,
						pTempAce,
						((PACE_HEADER)pTempAce)->AceSize
						))
					{
						LogMsg(TEXT("AddAce failed: %d"), GetLastError());
						throw 9;
					}
				}
			}
		}
		
		//
		// add the first ACE to the windowstation
		//
		pace = (ACCESS_ALLOWED_ACE *)new unsigned char [sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(psid) - sizeof(DWORD)];
		if (pace == NULL)
			throw 10;
		
		pace->Header.AceType  = ACCESS_ALLOWED_ACE_TYPE;
		pace->Header.AceFlags = CONTAINER_INHERIT_ACE | INHERIT_ONLY_ACE | OBJECT_INHERIT_ACE;
		pace->Header.AceSize  = sizeof(ACCESS_ALLOWED_ACE) + GetLengthSid(psid) - sizeof(DWORD);
		pace->Mask			  = GENERIC_ACCESS;
		
		if (!CopySid(GetLengthSid(psid), &pace->SidStart, psid))
		{
			LogMsg(TEXT("GLSid failed: %d"), GetLastError());
			throw 11;
		}
		
		if (!AddAce(pNewAcl, ACL_REVISION, MAXDWORD, (LPVOID)pace, pace->Header.AceSize))
		{
			LogMsg(TEXT("AddAce2 failed: %d"), GetLastError());
			throw 12;
		}
		
		//
		// add the second ACE to the windowstation
		//
		pace->Header.AceFlags = NO_PROPAGATE_INHERIT_ACE;
		pace->Mask			 = WINSTA_ALL;
		
		if (!AddAce(pNewAcl, ACL_REVISION, MAXDWORD, (LPVOID)pace, pace->Header.AceSize ))
		{
			LogMsg(TEXT("AddAce3 failed: %d"), GetLastError());
			throw 13;
		}
		
		//
		// set new dacl for the security descriptor
		//
		if (!SetSecurityDescriptorDacl(
			psdNew,
			TRUE,
			pNewAcl,
			FALSE
			))
		{
			LogMsg(TEXT("SSDDacl failed: %d"), GetLastError());
			throw 14;
		}
		
		//
		// set the new security descriptor for the windowstation
		//
		if (!SetUserObjectSecurity(hwinsta, &si, psdNew))
		{
			LogMsg(TEXT("SUOS failed: %d"), GetLastError());
			throw 15;
		}
	}
	catch(int e)
	{
		LogMsg(TEXT("throw int %d caught"), e);
		//
		// free the allocated buffers
		//
		if (pace != NULL)
			delete pace;
		
		if (pNewAcl != NULL)
			pNewAcl;
		
		if (psd != NULL)
			delete psd;
		
		if (psdNew != NULL)
			delete psdNew;

		return FALSE;
	}
	catch(...)
	{
		LogMsg(TEXT("throw ... caught"));
		//
		// free the allocated buffers
		//
		if (pace != NULL)
			delete pace;
		
		if (pNewAcl != NULL)
			pNewAcl;
		
		if (psd != NULL)
			delete psd;
		
		if (psdNew != NULL)
			delete psdNew;

		return FALSE;
	}
	
	return TRUE;
}

BOOL AddTheAceDesktop(HDESK hdesk, PSID psid)
{
	ACL_SIZE_INFORMATION aclSizeInfo;
	BOOL 				bDaclExist;
	BOOL 				bDaclPresent;
	BOOL 				bSuccess  = FALSE; // assume function will fail
	DWORD				dwNewAclSize;
	DWORD				dwSidSize = 0;
	DWORD				dwSdSizeNeeded;
	PACL 				pacl      = NULL;
	PACL 				pNewAcl   = NULL;
	PSECURITY_DESCRIPTOR psd 	  = NULL;
	PSECURITY_DESCRIPTOR psdNew	  = NULL;
	PVOID				pTempAce  = NULL;
	SECURITY_INFORMATION si		  = DACL_SECURITY_INFORMATION;
	unsigned int 		i;
	   
	try
	{
		//
		// obtain the security descriptor for the desktop object
		//
		if (!GetUserObjectSecurity(
			hdesk,
			&si,
			psd,
			dwSidSize,
			&dwSdSizeNeeded
			))
		{
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
			{
				psd = (PSECURITY_DESCRIPTOR)new unsigned char [dwSdSizeNeeded];
				if (psd == NULL)
					throw 16;
				
				psdNew = (PSECURITY_DESCRIPTOR)new unsigned char [dwSdSizeNeeded];
				if (psdNew == NULL)
					throw 17;
				
				dwSidSize = dwSdSizeNeeded;
				
				if (!GetUserObjectSecurity(
					hdesk,
					&si,
					psd,
					dwSidSize,
					&dwSdSizeNeeded
					))
					throw 18;
			}
			else
				throw 19;
		}
		   
		//
		// create a new security descriptor
		//
		if (!InitializeSecurityDescriptor(
			psdNew,
			SECURITY_DESCRIPTOR_REVISION
			))
			throw 20;
		
		//
		// obtain the dacl from the security descriptor
		//
		if (!GetSecurityDescriptorDacl(
			psd,
			&bDaclPresent,
			&pacl,
			&bDaclExist
			))
			throw 21;
		
		//
		// initialize
		//
		ZeroMemory(&aclSizeInfo, sizeof(ACL_SIZE_INFORMATION));
		aclSizeInfo.AclBytesInUse = sizeof(ACL);
		
		//
		// call only if NULL dacl
		//
		if (pacl != NULL)
		{
			//
			// determine the size of the ACL info
			//
			if (!GetAclInformation(
				pacl,
				(LPVOID)&aclSizeInfo,
				sizeof(ACL_SIZE_INFORMATION),
				AclSizeInformation
				))
				throw 22;
		}
		   
		//
		// compute the size of the new acl
		//
		dwNewAclSize = aclSizeInfo.AclBytesInUse +
			sizeof(ACCESS_ALLOWED_ACE) +
			GetLengthSid(psid) - sizeof(DWORD);
		
		//
		// allocate buffer for the new acl
		//
		pNewAcl = (PACL)new unsigned char [dwNewAclSize];
		if (pNewAcl == NULL)
			throw 23;
		
		//
		// initialize the new acl
		//
		if (!InitializeAcl(pNewAcl, dwNewAclSize, ACL_REVISION))
			throw 24;
		
		//
		// if DACL is present, copy it to a new DACL
		//
		if (bDaclPresent) // only copy if DACL was present
		{
			// copy the ACEs to our new ACL
			if (aclSizeInfo.AceCount)
			{
				for (i = 0; i < aclSizeInfo.AceCount; i++)
				{
					// get an ACE
					if (!GetAce(pacl, i, &pTempAce))
						throw 25;
					
					// add the ACE to the new ACL
					if (!AddAce(
						pNewAcl,
						ACL_REVISION,
						MAXDWORD,
						pTempAce,
						((PACE_HEADER)pTempAce)->AceSize
						))
						throw 26;
				}
			}
		}
		
		//
		// add ace to the dacl
		//
		if (!AddAccessAllowedAce(
			pNewAcl,
			ACL_REVISION,
			DESKTOP_ALL,
			psid
			))
			throw 27;
		
		//
		// set new dacl to the new security descriptor
		//
		if (!SetSecurityDescriptorDacl(
			psdNew,
			TRUE,
			pNewAcl,
			FALSE
			))
			throw 28;
		
		//
		// set the new security descriptor for the desktop object
		//
		if (!SetUserObjectSecurity(hdesk, &si, psdNew))
			throw 29;
	}
	catch(int e)
	{
		LogMsg(TEXT("throw int %d caught"), e);
		//
		// free buffers
		//
		if (pNewAcl != NULL)
			delete pNewAcl;
		
		if (psd != NULL)
			delete psd;
		
		if (psdNew != NULL)
			delete psdNew;

		return FALSE;
	}
	catch(...)
	{
		//
		// free buffers
		//
		if (pNewAcl != NULL)
			delete pNewAcl;
		
		if (psd != NULL)
			delete psd;
		
		if (psdNew != NULL)
			delete psdNew;

		return FALSE;
	}
	 
	return TRUE;
}
