#include "mpdimpl.h"
#include <wincrypt.h>
#include <tchar.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef MPD_REGISTRY_KEY
#define MPD_REGISTRY_KEY  TEXT("SOFTWARE\\MPICH\\MPD")
#endif

static char s_err_msg[1024];

static void sPrintError(int error, char *msg, ...)
{
    int n;
    va_list list;
    HLOCAL str;
    int num_bytes;

    va_start(list, msg);
    n = vsprintf(s_err_msg, msg, list);
    va_end(list);
    
    num_bytes = FormatMessage(
	FORMAT_MESSAGE_FROM_SYSTEM |
	FORMAT_MESSAGE_ALLOCATE_BUFFER,
	0,
	error,
	MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
	(LPTSTR) &str,
	0,0);
    
    strcat(s_err_msg, "Error Text: ");
    strcat(s_err_msg, (const char *)str);
    
    LocalFree(str);
}

char *mpdCryptGetLastErrorString()
{
    return s_err_msg;
}

void InitMPDUser()
{
    char value[100];
    if (ReadMPDRegistry("mpdUserCapable", value))
    {
	if (stricmp(value, "yes") == 0)
	    g_bMPDUserCapable = true;
    }
    if (g_bMPDUserCapable)
    {
	ReadMPDRegistry("UseMPDUser", value);
	if (stricmp(value, "yes") == 0)
	{
	    g_bUseMPDUser = mpdReadPasswordFromRegistry(g_pszMPDUserAccount, g_pszMPDUserPassword);
	}
	else
	{
	    g_bUseMPDUser = false;
	}
    }
}

bool mpdSetupCryptoClient()
{
	// Ensure that the default cryptographic client is set up.
	HCRYPTPROV hProv;
	HCRYPTKEY hKey;
	int nError;
	
	// Attempt to acquire a handle to the default key container.
	//if (!CryptAcquireContext(&hProv, NULL, MS_DEF_PROV, PROV_RSA_FULL, 0))
	if (!CryptAcquireContext(&hProv, "MPICH", MS_DEF_PROV, PROV_RSA_FULL, 0))
	{
		// Some sort of error occured, create default key container.
		//if (!CryptAcquireContext(&hProv, NULL, MS_DEF_PROV, PROV_RSA_FULL, CRYPT_NEWKEYSET))
		if (!CryptAcquireContext(&hProv, "MPICH", MS_DEF_PROV, PROV_RSA_FULL, CRYPT_NEWKEYSET))
		{
			// Error creating key container!
			nError = GetLastError();
			sPrintError(nError, "mpdSetupCryptoClient:CryptAcquireContext(...) failed, error: %d\n", nError);
			return false;
		}
	}

	// Attempt to get handle to signature key.
	if (!CryptGetUserKey(hProv, AT_SIGNATURE, &hKey))
	{
		if ((nError = GetLastError()) == NTE_NO_KEY)
		{
			// Create signature key pair.
			if (!CryptGenKey(hProv, AT_SIGNATURE, 0, &hKey))
			{
				// Error during CryptGenKey!
				nError = GetLastError();
				CryptReleaseContext(hProv, 0);
				sPrintError(nError, "mpdSetupCryptoClient:CryptGenKey(...) failed, error: %d\n", nError);
				return false;
			}
			else
			{
				CryptDestroyKey(hKey);
			}
		}
		else 
		{
			// Error during CryptGetUserKey!
			CryptReleaseContext(hProv, 0);
			sPrintError(nError, "mpdSetupCryptoClient:CryptGetUserKey(...) failed, error: %d\n", nError);
			return false;
		}
	}

	// Attempt to get handle to exchange key.
	if (!CryptGetUserKey(hProv,AT_KEYEXCHANGE,&hKey))
	{
		if ((nError = GetLastError()) == NTE_NO_KEY)
		{
			// Create key exchange key pair.
			if (!CryptGenKey(hProv,AT_KEYEXCHANGE,0,&hKey))
			{
				// Error during CryptGenKey!
				nError = GetLastError();
				CryptReleaseContext(hProv, 0);
				sPrintError(nError, "mpdSetupCryptoClient:CryptGenKey(...) failed, error: %d\n", nError);
				return false;
			}
			else
			{
				CryptDestroyKey(hKey);
			}
		}
		else
		{
			// Error during CryptGetUserKey!
			CryptReleaseContext(hProv, 0);
			sPrintError(nError, "mpdSetupCryptoClient:CryptGetUserKey(...) failed, error: %d\n", nError);
			return false;
		}
	}

	CryptReleaseContext(hProv, 0);
	return true;
}

bool mpdDeletePasswordRegistryEntry()
{
	int nError;
	HKEY hRegKey = NULL;

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY, 0, KEY_ALL_ACCESS, &hRegKey) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		sPrintError(nError, "mpdDeletePasswordRegistryEntry:RegOpenKeyEx(...) failed, error: %d\n", nError);
		return false;
	}

	if (RegDeleteValue(hRegKey, TEXT("mpdUserPassword")) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		sPrintError(nError, "mpdDeletePasswordRegistryEntry:RegDeleteValue(...) failed, error: %d\n", nError);
		RegCloseKey(hRegKey);
		return false;
	}

	if (RegDeleteValue(hRegKey, TEXT("mpdUserAccount")) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		sPrintError(nError, "mpdDeletePasswordRegistryEntry:RegDeleteValue(...) failed, error: %d\n", nError);
		RegCloseKey(hRegKey);
		return false;
	}

	return true;
}

bool mpdSavePasswordToRegistry(TCHAR *szAccount, TCHAR *szPassword, bool persistent)
{
	int nError;
	bool bResult = true;
	
	TCHAR szKey[256];
	HKEY hRegKey = NULL;
	_tcscpy(szKey, MPD_REGISTRY_KEY);
	
	if (persistent)
	{
		if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szKey,
			0, 
			NULL, 
			REG_OPTION_NON_VOLATILE,
			KEY_ALL_ACCESS, 
			NULL,
			&hRegKey, 
			NULL) != ERROR_SUCCESS) 
		{
			nError = GetLastError();
			sPrintError(nError, "mpdSavePasswordToRegistry:RegCreateKeyEx(...) failed, error: %d\n", nError);
			return false;
		}
	}
	else
	{
		RegDeleteKey(HKEY_LOCAL_MACHINE, szKey);
		if (RegCreateKeyEx(HKEY_LOCAL_MACHINE, szKey,
			0, 
			NULL, 
			REG_OPTION_VOLATILE,
			KEY_ALL_ACCESS, 
			NULL,
			&hRegKey, 
			NULL) != ERROR_SUCCESS) 
		{
			nError = GetLastError();
			sPrintError(nError, "mpdSavePasswordToRegistry:RegDeleteKey(...) failed, error: %d\n", nError);
			return false;
		}
	}

	// Store the account name
	if (::RegSetValueEx(
			hRegKey, _T("mpdUserAccount"), 0, REG_SZ, 
			(BYTE*)szAccount, 
			sizeof(TCHAR)*(_tcslen(szAccount)+1)
			)!=ERROR_SUCCESS)
	{
		nError = GetLastError();
		sPrintError(nError, "mpdSavePasswordToRegistry:RegSetValueEx(...) failed, error: %d\n", nError);
		::RegCloseKey(hRegKey);
		return false;
	}

	HCRYPTPROV hProv = (HCRYPTPROV)NULL;
	HCRYPTKEY hKey = (HCRYPTKEY)NULL;
	HCRYPTKEY hXchgKey = (HCRYPTKEY)NULL;
	HCRYPTHASH hHash = (HCRYPTHASH)NULL;
	DWORD dwLength;
	// Used to encrypt the real password
	TCHAR szLocalPassword[] = _T("mMpMdPzI6C@HaA0NiL*I%Ll");

	// Get handle to user default provider.
	//if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, 0))
	if (CryptAcquireContext(&hProv, "MPICH", NULL, PROV_RSA_FULL, 0))
	{
		// Create hash object.
		if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
		{
			// Hash password string.
			dwLength = sizeof(TCHAR)*_tcslen(szLocalPassword);
			if (CryptHashData(hHash, (BYTE *)szLocalPassword, dwLength, 0))
			{
				// Create block cipher session key based on hash of the password.
				if (CryptDeriveKey(hProv, CALG_RC4, hHash, CRYPT_EXPORTABLE, &hKey))
				{
					// Determine number of bytes to encrypt at a time.
					dwLength = sizeof(TCHAR)*(_tcslen(szPassword)+1);

					// Allocate memory.
					BYTE *pbBuffer = (BYTE *)malloc(dwLength);
					if (pbBuffer != NULL)
					{
						memcpy(pbBuffer, szPassword, dwLength);
						// Encrypt data
						if (CryptEncrypt(hKey, 0, TRUE, 0, pbBuffer, &dwLength, dwLength)) 
						{
							// Write data to registry.
							DWORD dwType = REG_BINARY;
							// Add the password.
							if (::RegSetValueEx(hRegKey, _T("mpdUserPassword"), 0, REG_BINARY, pbBuffer, dwLength)!=ERROR_SUCCESS)
							{
								nError = GetLastError();
								sPrintError(nError, "mpdSavePasswordToRegistry:RegSetValueEx(...) failed, error: %d\n", nError);
								bResult = false;
							}
							::RegCloseKey(hRegKey);
						}	
						else
						{
							nError = GetLastError();
							sPrintError(nError, "mpdSavePasswordToRegistry:CryptEncrypt(...) failed, error: %d\n", nError);
							bResult = false;
						}
						// Free memory.
					  free(pbBuffer);
					}
					else
					{
						nError = GetLastError();
						sPrintError(nError, "mpdSavePasswordToRegistry:malloc(...) failed, error: %d\n", nError);
						bResult = false;
					}
					CryptDestroyKey(hKey);  // Release provider handle.
				}
				else
				{
					// Error during CryptDeriveKey!
					nError = GetLastError();
					sPrintError(nError, "mpdSavePasswordToRegistry:CryptDeriveKey(...) failed, error: %d\n", nError);
					bResult = false;
				}
			}
			else
			{
				// Error during CryptHashData!
				nError = GetLastError();
				sPrintError(nError, "mpdSavePasswordToRegistry:CryptHashData(...) failed, error: %d\n", nError);
				bResult = false;
			}
			CryptDestroyHash(hHash); // Destroy session key.
		}
		else
		{
			// Error during CryptCreateHash!
			nError = GetLastError();
			sPrintError(nError, "mpdSavePasswordToRegistry:CryptCreateHash(...) failed, error: %d\n", nError);
			bResult = false;
		}
		CryptReleaseContext(hProv, 0);
	}

	return bResult;
}

// The following function reads the password from the registry and decrypts it. 
// Note that the szPassword parameter should be already allocated with a minimum 
// size of 32 characters (64 bytes if using UNICODE). 
// The account buffer must be able to hold 100 characters.
bool mpdReadPasswordFromRegistry(TCHAR *szAccount, TCHAR *szPassword) 
{
	int nError;
	bool bResult = true;
	
	TCHAR szKey[256];
	HKEY hRegKey = NULL;
	_tcscpy(szKey, MPD_REGISTRY_KEY);
	
	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, szKey, 0, KEY_QUERY_VALUE, &hRegKey) == ERROR_SUCCESS) 
	{
		DWORD dwLength = 100;
		*szAccount = TEXT('\0');
		if (RegQueryValueEx(
				hRegKey, 
				_T("mpdUserAccount"), NULL, 
				NULL, 
				(BYTE*)szAccount, 
				&dwLength)!=ERROR_SUCCESS)
		{
			nError = GetLastError();
			sPrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
			::RegCloseKey(hRegKey);
			return false;
		}
		if (_tcslen(szAccount) < 1)
		{
			sPrintError(-1, "Empty account name stored in registry is not valid.\n");
			return false;
		}

		HCRYPTPROV hProv = (HCRYPTPROV)NULL;
		HCRYPTKEY hKey = (HCRYPTKEY)NULL;
		HCRYPTKEY hXchgKey = (HCRYPTKEY)NULL;
		HCRYPTHASH hHash = (HCRYPTHASH)NULL;
		// has to be the same used to encrypt!
		TCHAR szLocalPassword[] = _T("mMpMdPzI6C@HaA0NiL*I%Ll");

		// Get handle to user default provider.
		//if (CryptAcquireContext(&hProv, NULL, NULL, PROV_RSA_FULL, 0))
		if (CryptAcquireContext(&hProv, "MPICH", NULL, PROV_RSA_FULL, 0))
		{
			// Create hash object.
			if (CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash))
			{
				// Hash password string.
				dwLength = sizeof(TCHAR)*_tcslen(szLocalPassword);
				if (CryptHashData(hHash, (BYTE *)szLocalPassword, dwLength, 0))
				{
					// Create block cipher session key based on hash of the password.
					if (CryptDeriveKey(hProv, CALG_RC4, hHash, CRYPT_EXPORTABLE, &hKey))
					{
						// the password is less than 32 characters
						dwLength = 32*sizeof(TCHAR);
						DWORD dwType = REG_BINARY;
						if (RegQueryValueEx(hRegKey, _T("mpdUserPassword"), NULL, &dwType, (BYTE*)szPassword, &dwLength)==ERROR_SUCCESS)
						{
							if (!CryptDecrypt(hKey, 0, TRUE, 0, (BYTE *)szPassword, &dwLength))
							{
								nError = GetLastError();
								sPrintError(nError, "ReadPasswordFromRegistry:CryptDecrypt(...) failed, error: %d\n", nError);
								bResult = false;
							}
						}
						else
						{
							nError = GetLastError();
							sPrintError(nError, "ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
							bResult = false;
						}
						CryptDestroyKey(hKey);  // Release provider handle.
					}
					else
					{
						// Error during CryptDeriveKey!
						nError = GetLastError();
						sPrintError(nError, "ReadPasswordFromRegistry:CryptDeriveKey(...) failed, error: %d\n", nError);
						bResult = false;
					}
				}
				else
				{
					// Error during CryptHashData!
					nError = GetLastError();
					sPrintError(nError, "ReadPasswordFromRegistry:CryptHashData(...) failed, error: %d\n", nError);
					bResult = false;
				}
				CryptDestroyHash(hHash); // Destroy session key.
			}
			else
			{
				// Error during CryptCreateHash!
				nError = GetLastError();
				sPrintError(nError, "ReadPasswordFromRegistry:CryptCreateHash(...) failed, error: %d\n", nError);
				bResult = false;
			}
			CryptReleaseContext(hProv, 0);
		}
		::RegCloseKey(hRegKey);
	}	
	else
	{
		nError = GetLastError();
		sPrintError(nError, "ReadPasswordFromRegistry:RegOpenKeyEx(...) failed, error: %d\n", nError);
		bResult = false;
	}

	return bResult;
}
