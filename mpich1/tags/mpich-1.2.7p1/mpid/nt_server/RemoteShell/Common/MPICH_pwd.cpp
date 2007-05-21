//#include "stdafx.h"
#include "MPICH_pwd.h"
#include <wincrypt.h>
#include <tchar.h>
#include "MPIJobdefs.h"
#include <stdio.h>

BOOL SetupCryptoClient()
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
			printf("SetupCryptoClient:CryptAcquireContext(...) failed, error: %d\n", nError);
			return FALSE;
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
				printf("SetupCryptoClient:CryptGenKey(...) failed, error: %d\n", nError);
				return FALSE;
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
			printf("SetupCryptoClient:CryptGetUserKey(...) failed, error: %d\n", nError);
			return FALSE;
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
				printf("SetupCryptoClient:CryptGenKey(...) failed, error: %d\n", nError);
				return FALSE;
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
			printf("SetupCryptoClient:CryptGetUserKey(...) failed, error: %d\n", nError);
			return FALSE;
		}
	}

	CryptReleaseContext(hProv, 0);
	return TRUE;
}

BOOL DeleteCurrentPasswordRegistryEntry()
{
	int nError;
	HKEY hRegKey = NULL;

	if (RegOpenKeyEx(HKEY_CURRENT_USER, MPICHKEY, 0, KEY_ALL_ACCESS, &hRegKey) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		printf("DeleteCurrentPasswordRegistryEntry:RegOpenKeyEx(...) failed, error: %d\n", nError);
		return FALSE;
	}

	if (RegDeleteValue(hRegKey, TEXT("Password")) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		printf("DeleteCurrentPasswordRegistryEntry:RegDeleteValue(...) failed, error: %d\n", nError);
		RegCloseKey(hRegKey);
		return FALSE;
	}

	if (RegDeleteValue(hRegKey, TEXT("Account")) != ERROR_SUCCESS)
	{
		nError = GetLastError();
		printf("DeleteCurrentPasswordRegistryEntry:RegDeleteValue(...) failed, error: %d\n", nError);
		RegCloseKey(hRegKey);
		return FALSE;
	}

	RegCloseKey(hRegKey);

	RegDeleteKey(HKEY_CURRENT_USER, MPICHKEY);

	return TRUE;
}

BOOL SavePasswordToRegistry(TCHAR *szAccount, TCHAR *szPassword, bool persistent)
{
	int nError;
	BOOL bResult = TRUE;
	
	TCHAR szKey[256];
	HKEY hRegKey = NULL;
	_tcscpy(szKey, MPICHKEY);
	
	if (persistent)
	{
		if (RegCreateKeyEx(HKEY_CURRENT_USER, szKey,
			0, 
			NULL, 
			REG_OPTION_NON_VOLATILE,
			KEY_ALL_ACCESS, 
			NULL,
			&hRegKey, 
			NULL) != ERROR_SUCCESS) 
		{
			nError = GetLastError();
			printf("SavePasswordToRegistry:RegCreateKeyEx(...) failed, error: %d\n", nError);
			return FALSE;
		}
	}
	else
	{
		RegDeleteKey(HKEY_CURRENT_USER, szKey);
		if (RegCreateKeyEx(HKEY_CURRENT_USER, szKey,
			0, 
			NULL, 
			REG_OPTION_VOLATILE,
			KEY_ALL_ACCESS, 
			NULL,
			&hRegKey, 
			NULL) != ERROR_SUCCESS) 
		{
			nError = GetLastError();
			printf("SavePasswordToRegistry:RegDeleteKey(...) failed, error: %d\n", nError);
			return FALSE;
		}
	}

	// Store the account name
	if (::RegSetValueEx(
			hRegKey, _T("Account"), 0, REG_SZ, 
			(BYTE*)szAccount, 
			sizeof(TCHAR)*(_tcslen(szAccount)+1)
			)!=ERROR_SUCCESS)
	{
		nError = GetLastError();
		printf("SavePasswordToRegistry:RegSetValueEx(...) failed, error: %d\n", nError);
		::RegCloseKey(hRegKey);
		return FALSE;
	}

	HCRYPTPROV hProv = NULL;
	HCRYPTKEY hKey = NULL;
	HCRYPTKEY hXchgKey = NULL;
	HCRYPTHASH hHash = NULL;
	DWORD dwLength;
	// Used to encrypt the real password
	TCHAR szLocalPassword[] = _T("MMPzI6C@HaA0NiL*I%Ll");

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
							if (::RegSetValueEx(hRegKey, _T("Password"), 0, REG_BINARY, pbBuffer, dwLength)!=ERROR_SUCCESS)
							{
								nError = GetLastError();
								printf("SavePasswordToRegistry:RegSetValueEx(...) failed, error: %d\n", nError);
								bResult = FALSE;
							}
							::RegCloseKey(hRegKey);
						}	
						else
						{
							nError = GetLastError();
							printf("SavePasswordToRegistry:CryptEncrypt(...) failed, error: %d\n", nError);
							bResult = FALSE;
						}
						// Free memory.
					  free(pbBuffer);
					}
					else
					{
						nError = GetLastError();
						printf("SavePasswordToRegistry:malloc(...) failed, error: %d\n", nError);
						bResult = FALSE;
					}
					CryptDestroyKey(hKey);  // Release provider handle.
				}
				else
				{
					// Error during CryptDeriveKey!
					nError = GetLastError();
					printf("SavePasswordToRegistry:CryptDeriveKey(...) failed, error: %d\n", nError);
					bResult = FALSE;
				}
			}
			else
			{
				// Error during CryptHashData!
				nError = GetLastError();
				printf("SavePasswordToRegistry:CryptHashData(...) failed, error: %d\n", nError);
				bResult = FALSE;
			}
			CryptDestroyHash(hHash); // Destroy session key.
		}
		else
		{
			// Error during CryptCreateHash!
			nError = GetLastError();
			printf("SavePasswordToRegistry:CryptCreateHash(...) failed, error: %d\n", nError);
			bResult = FALSE;
		}
		CryptReleaseContext(hProv, 0);
	}

	return bResult;
}

// The following function reads the password from the registry and decrypts it. 
// Note that the szPassword parameter should be already allocated with a minimum 
// size of 32 characters (64 bytes if using UNICODE). 
// The account buffer must be able to hold 100 characters.
BOOL ReadPasswordFromRegistry(TCHAR *szAccount, TCHAR *szPassword) 
{
	int nError;
	BOOL bResult = TRUE;
	
	TCHAR szKey[256];
	HKEY hRegKey = NULL;
	_tcscpy(szKey, MPICHKEY);
	
	if (RegOpenKeyEx(HKEY_CURRENT_USER, szKey, 0, KEY_QUERY_VALUE, &hRegKey) == ERROR_SUCCESS) 
	{
		DWORD dwLength = 100;
		*szAccount = TEXT('\0');
		if (RegQueryValueEx(
				hRegKey, 
				_T("Account"), NULL, 
				NULL, 
				(BYTE*)szAccount, 
				&dwLength)!=ERROR_SUCCESS)
		{
			nError = GetLastError();
			//printf("ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
			::RegCloseKey(hRegKey);
			return FALSE;
		}
		if (_tcslen(szAccount) < 1)
			return FALSE;

		HCRYPTPROV hProv = NULL;
		HCRYPTKEY hKey = NULL;
		HCRYPTKEY hXchgKey = NULL;
		HCRYPTHASH hHash = NULL;
		// has to be the same used to encrypt!
		TCHAR szLocalPassword[] = _T("MMPzI6C@HaA0NiL*I%Ll");

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
						if (RegQueryValueEx(hRegKey, _T("Password"), NULL, &dwType, (BYTE*)szPassword, &dwLength)==ERROR_SUCCESS)
						{
							if (!CryptDecrypt(hKey, 0, TRUE, 0, (BYTE *)szPassword, &dwLength))
							{
								nError = GetLastError();
								//printf("ReadPasswordFromRegistry:CryptDecrypt(...) failed, error: %d\n", nError);
								bResult = FALSE;
							}
						}
						else
						{
							nError = GetLastError();
							//printf("ReadPasswordFromRegistry:RegQueryValueEx(...) failed, error: %d\n", nError);
							bResult = FALSE;
						}
						CryptDestroyKey(hKey);  // Release provider handle.
					}
					else
					{
						// Error during CryptDeriveKey!
						nError = GetLastError();
						//printf("ReadPasswordFromRegistry:CryptDeriveKey(...) failed, error: %d\n", nError);
						bResult = FALSE;
					}
				}
				else
				{
					// Error during CryptHashData!
					nError = GetLastError();
					//printf("ReadPasswordFromRegistry:CryptHashData(...) failed, error: %d\n", nError);
					bResult = FALSE;
				}
				CryptDestroyHash(hHash); // Destroy session key.
			}
			else
			{
				// Error during CryptCreateHash!
				nError = GetLastError();
				//printf("ReadPasswordFromRegistry:CryptCreateHash(...) failed, error: %d\n", nError);
				bResult = FALSE;
			}
			CryptReleaseContext(hProv, 0);
		}
		::RegCloseKey(hRegKey);
	}	
	else
	{
		nError = GetLastError();
		//printf("ReadPasswordFromRegistry:RegOpenKeyEx(...) failed, error: %d\n", nError);
		bResult = FALSE;
	}

	return bResult;
}

