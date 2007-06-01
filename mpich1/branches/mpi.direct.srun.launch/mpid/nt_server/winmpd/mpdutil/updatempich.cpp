#include "mpdutil.h"
#include "mpd.h"
#include "Translate_Error.h"
#include "Service.h"

bool UpdateMPICH(
	       const char *pszHost, 
	       const char *pszAccount, 
	       const char *pszPassword, 
	       int nPort, 
	       const char *pszPhrase, 
	       const char *pszFileName,
	       const char *pszFileNamed,
	       char *pszError,
	       int nErrLen)
{
    SOCKET sock;
    char pszStr[MAX_CMD_LENGTH];
    char pszTempFileName[MAX_PATH];
    char pszTempFileNamed[MAX_PATH];
    int ret_val;
    char *pszEncoded;

    // Connect to the mpd on pszHost
    ret_val = ConnectToMPD(pszHost, nPort, pszPhrase, &sock);
    if (ret_val != 0)
    {
	_snprintf(pszError, nErrLen, "Unable to connect to %s\n", pszHost);
	return false;
    }

    // Initialize the file operations
    pszEncoded = EncodePassword((char*)pszPassword);
    _snprintf(pszStr, MAX_CMD_LENGTH, "fileinit account=%s password=%s", pszAccount, pszEncoded);
    if (pszEncoded != NULL) free(pszEncoded);

    ret_val = WriteString(sock, pszStr);
    if (ret_val == SOCKET_ERROR)
    {
	printf("Writing the fileinit command failed, error %d\n", WSAGetLastError());
	easy_closesocket(sock);
	return false;
    }

    // Create a temporary file for mpich.dll
    _snprintf(pszStr, MAX_CMD_LENGTH, "createtmpfile host=%s delete=no", pszHost);
    ret_val = WriteString(sock, pszStr);
    if (ret_val == SOCKET_ERROR)
    {
	_snprintf(pszError, nErrLen, "Writing the createtempfile command failed on %s, error %d\n", pszHost, WSAGetLastError());
	easy_closesocket(sock);
	return false;
    }
    if (!ReadString(sock, pszTempFileName))
    {
	_snprintf(pszError, nErrLen, "Reading the temporary file name failed\n");
	easy_closesocket(sock);
	return false;
    }

    // Copy the new mpich.dll into this temporary file
    _snprintf(pszStr, MAX_CMD_LENGTH, "local='%s' remote='%s'", pszFileName, pszTempFileName);
    if (!PutFile(sock, pszStr))
    {
	_snprintf(pszStr, MAX_CMD_LENGTH, "deletetmpfile host=%s file='%s'", pszHost, pszTempFileName);
	WriteString(sock, pszStr);
	ReadString(sock, pszStr);
	WriteString(sock, "done");
	_snprintf(pszError, nErrLen, "Unable to put the new mpich.dll file on host %s", pszHost);
	easy_closesocket(sock);
	return false;
    }

    // Update the mpich.dll
    _snprintf(pszStr, MAX_CMD_LENGTH, "updatempich %s", pszTempFileName);
    ret_val = WriteString(sock, pszStr);
    if (ret_val == SOCKET_ERROR)
    {
	_snprintf(pszError, nErrLen, "Writing the updatempich command failed, error %d", WSAGetLastError());
	easy_closesocket(sock);
	return false;
    }
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	_snprintf(pszError, nErrLen, "Reading the result of the updatempich command failed\n");
	easy_closesocket(sock);
	return false;
    }
    if (stricmp(pszStr, "SUCCESS") != 0)
    {
	_snprintf(pszError, nErrLen, "updatempich returned an error: %s\n", pszStr);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return false;
    }

    // Create a temporary file for mpichd.dll
    _snprintf(pszStr, MAX_CMD_LENGTH, "createtmpfile host=%s delete=no", pszHost);
    ret_val = WriteString(sock, pszStr);
    if (ret_val == SOCKET_ERROR)
    {
	_snprintf(pszError, nErrLen, "Writing the createtempfile command failed on %s, error %d\n", pszHost, WSAGetLastError());
	easy_closesocket(sock);
	return false;
    }
    if (!ReadString(sock, pszTempFileNamed))
    {
	_snprintf(pszError, nErrLen, "Reading the temporary file name failed\n");
	easy_closesocket(sock);
	return false;
    }

    // Copy the new mpichd.dll into this temporary file
    _snprintf(pszStr, MAX_CMD_LENGTH, "local='%s' remote='%s'", pszFileNamed, pszTempFileNamed);
    if (!PutFile(sock, pszStr))
    {
	_snprintf(pszStr, MAX_CMD_LENGTH, "deletetmpfile host=%s file='%s'", pszHost, pszTempFileNamed);
	WriteString(sock, pszStr);
	ReadString(sock, pszStr);
	WriteString(sock, "done");
	_snprintf(pszError, nErrLen, "Unable to put the new mpichd.dll file on host %s", pszHost);
	easy_closesocket(sock);
	return false;
    }

    // Update the mpichd.dll
    _snprintf(pszStr, MAX_CMD_LENGTH, "updatempichd %s", pszTempFileNamed);
    ret_val = WriteString(sock, pszStr);
    if (ret_val == SOCKET_ERROR)
    {
	_snprintf(pszError, nErrLen, "Writing the updatempichd command failed, error %d", WSAGetLastError());
	easy_closesocket(sock);
	return false;
    }
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	_snprintf(pszError, nErrLen, "Reading the result of the updatempichd command failed\n");
	easy_closesocket(sock);
	return false;
    }
    if (stricmp(pszStr, "SUCCESS") != 0)
    {
	_snprintf(pszError, nErrLen, "updatempichd returned an error: %s\n", pszStr);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return false;
    }

    // Close the console session
    WriteString(sock, "done");
    easy_closesocket(sock);

    return true;
}
