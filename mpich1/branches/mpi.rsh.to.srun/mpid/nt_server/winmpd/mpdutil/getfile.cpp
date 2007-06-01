#include "mpdutil.h"
#include "mpd.h"
#include "GetStringOpt.h"
#include "Translate_Error.h"

#define MAX_FILENAME MAX_PATH * 2

void GetFile(int sock, char *pszInputStr)
{
    bool bReplace = true, bCreateDir = false;
    char pszFileName[MAX_FILENAME];
    char pszRemoteFileName[MAX_FILENAME];
    char pszStr[MAX_FILENAME];
    int nLength;
    FILE *fout;
    char pBuffer[TRANSFER_BUFFER_SIZE];
    int nNumRead;
    int nNumWritten;
    bool bLocal = true, bRemote = true;

    // Parse the string for parameters
    if (GetStringOpt(pszInputStr, "replace", pszStr))
    {
	bReplace = (stricmp(pszStr, "yes") == 0);
    }
    if (GetStringOpt(pszInputStr, "createdir", pszStr))
    {
	bCreateDir = (stricmp(pszStr, "yes") == 0);
    }
    bLocal = GetStringOpt(pszInputStr, "local", pszFileName);
    bRemote = GetStringOpt(pszInputStr, "remote", pszRemoteFileName);

    if (!bLocal && !bRemote)
    {
	printf("Error: no file name provided\n");
	return;
    }
    if (!bRemote)
	strncpy(pszRemoteFileName, pszFileName, MAX_FILENAME);
    if (!bLocal)
	strncpy(pszFileName, pszRemoteFileName, MAX_FILENAME);

    // Create the local file
    //dbg_printf("creating file '%s'\n", pszFileName);
    if (bCreateDir)
    {
	if (!TryCreateDir(pszFileName, pszStr))
	{
	    printf("Error: unable to create the directory, %s\n", pszStr);
	    return;
	}
    }

    if (!bReplace)
    {
	fout = fopen(pszFileName, "r");
	if (fout != NULL)
	{
	    printf("Error: file exists\n");
	    fclose(fout);
	    return;
	}
	fclose(fout);
    }

    fout = fopen(pszFileName, "wb");

    if (fout == NULL)
    {
	Translate_Error(GetLastError(), pszStr, "Error: Unable to open the file, ");
	printf("%s\n", pszStr);
	return;
    }

    // Send the getfile command
    _snprintf(pszStr, MAX_FILENAME, "getfile name=%s", pszRemoteFileName);
    if (WriteString(sock, pszStr) == SOCKET_ERROR)
    {
	Translate_Error(WSAGetLastError(), pszStr, "Error: Writing getfile command failed, ");
	printf("%s\n", pszStr);
	fclose(fout);
	return;
    }

    if (!ReadString(sock, pszStr))
    {
	printf("Error: failed to read the response from the getfile command.\n");
	fclose(fout);
	return;
    }

    nLength = atoi(pszStr);
    if (nLength == -1)
    {
	if (!ReadString(sock, pszStr))
	{
	    printf("Error: failed to read the error message from the getfile command.\n");
	    fclose(fout);
	    return;
	}
	printf("Error: %s\n", pszStr);
	fclose(fout);
	return;
    }

    while (nLength)
    {
	nNumRead = min(nLength, TRANSFER_BUFFER_SIZE);
	if (easy_receive(sock, pBuffer, nNumRead) == SOCKET_ERROR)
	{
	    err_printf("ERROR: easy_receive failed, error %d\n", WSAGetLastError());
	    fclose(fout);
	    DeleteFile(pszFileName);
	    return;
	}
	nNumWritten = fwrite(pBuffer, 1, nNumRead, fout);
	if (nNumWritten != nNumRead)
	{
	    err_printf("ERROR: received %d bytes but only wrote %d bytes\n", nNumRead, nNumWritten);
	}
	//dbg_printf("%d bytes read, %d bytes written\n", nNumRead, nNumWritten);
	nLength -= nNumRead;
    }

    fclose(fout);

    printf("SUCCESS\n");
}
