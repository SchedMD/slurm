#ifndef MPICH_PWD_H
#define MPICH_PWD_H

#include <windows.h>

BOOL SetupCryptoClient();
BOOL SavePasswordToRegistry(TCHAR *szAccount, TCHAR *szPassword, bool persistent=true);
BOOL ReadPasswordFromRegistry(TCHAR *szAccount, TCHAR *szPassword);
BOOL DeleteCurrentPasswordRegistryEntry();

#endif
