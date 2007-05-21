#include "mpdutil.h"
#include "mpd.h"
#include "GetStringOpt.h"
#include "Translate_Error.h"

#define MAX_FILENAME MAX_PATH * 2

bool PutFile(int sock, char *pszInputStr)
{
    char pszFileName[MAX_FILENAME];
    char pszRemoteFileName[MAX_FILENAME];
    char pszReplace[10] = "yes";
    char pszCreateDir[10] = "yes";
    char pszStr[MAX_CMD_LENGTH];
    FILE *fin;
    int error;
    int nLength;

    // Parse the input string
    if (!GetStringOpt(pszInputStr, "local", pszFileName))
    {
	printf("Error: no local file name specified (local=filename).\n");
	return false;
    }

    if (!GetStringOpt(pszInputStr, "remote", pszRemoteFileName))
    {
	strncpy(pszRemoteFileName, pszFileName, MAX_FILENAME);
    }

    GetStringOpt(pszInputStr, "replace", pszReplace);
    GetStringOpt(pszInputStr, "createdir", pszCreateDir);

    // Open the file
    fin = fopen(pszFileName, "rb");
    if (fin == NULL)
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	printf("Unable to open local file:\nFile: '%s'\nError: %s\n", pszFileName, pszStr);
	return false;
    }

    // Get the size
    fseek(fin, 0, SEEK_END);
    nLength = ftell(fin);
    if (nLength == -1)
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	printf("Unable to determine the size of the local file:\nFile: '%s'\nError: %s\n", pszFileName, pszStr);
	return false;
    }

    // Rewind back to the beginning
    fseek(fin, 0, SEEK_SET);

    // Send the putfile command
    _snprintf(pszStr, MAX_CMD_LENGTH, "putfile name=%s length=%d replace=%s createdir=%s", pszRemoteFileName, nLength, pszReplace, pszCreateDir);
    WriteString(sock, pszStr);

    // Get the response
    ReadString(sock, pszStr);
    if (strcmp(pszStr, "SEND") == 0)
    {
	// Send the data
	char pBuffer[TRANSFER_BUFFER_SIZE];
	int nNumRead;
	while (nLength)
	{
	    nNumRead = min(nLength, TRANSFER_BUFFER_SIZE);
	    nNumRead = fread(pBuffer, 1, nNumRead, fin);
	    if (nNumRead < 1)
	    {
		printf("fread failed, %d\n", ferror(fin));
		easy_closesocket(sock);
		ExitProcess(0);
	    }
	    easy_send(sock, pBuffer, nNumRead);
	    //printf("%d bytes sent\n", nNumRead);fflush(stdout);
	    nLength -= nNumRead;
	}

	ReadString(sock, pszStr);
	//printf("%s\n", pszStr);
	if (strcmp(pszStr, "SUCCESS") == 0)
	{
	    fclose(fin);
	    return true;
	}
    }
    else
    {
	printf("%s\n", pszStr);
    }

    fclose(fin);
    return false;
}
