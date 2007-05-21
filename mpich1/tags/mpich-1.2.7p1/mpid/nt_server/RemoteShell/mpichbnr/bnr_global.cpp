#include "bnr_internal.h"

HANDLE g_hMPDPipe = NULL;
HANDLE g_hMPDOutputPipe = NULL;
HANDLE g_hMPDEndOutputPipe = NULL;
BNR_Group g_bnrGroup = BNR_INVALID_GROUP;
BNR_Group g_bnrParent = BNR_INVALID_GROUP;

// Function name	: GetString
// Description	    : 
// Return type		: int 
// Argument         : HANDLE hInput
// Argument         : char *pBuffer
int GetString(HANDLE hInput, char *pBuffer)
{
	DWORD dwNumRead;
	if (pBuffer == NULL)
		return -1;
	*pBuffer = '\n';

	// Ignore any leading CR/LF bytes
	while (*pBuffer == '\r' || *pBuffer == '\n')
	{
		if (!ReadFile(hInput, pBuffer, 1, &dwNumRead, NULL))
		{
			*pBuffer = '\0';
			return GetLastError();
		}
	}

	//printf("%c", pBuffer);fflush(stdout);
	// Read bytes until reaching a CR or LF
	do
	{
		pBuffer++;
		if (!ReadFile(hInput, pBuffer, 1, &dwNumRead, NULL))
		{
			*pBuffer = '\0';
			return GetLastError();
		}
		//printf("%c", pBuffer);fflush(stdout);
	} while (*pBuffer != '\r' && *pBuffer != '\n');

	// Should I check to see if there is another character?
	// Do I assume that the lines will be separated by two character or just one?  CR and LF
	// If there are two characters then maybe I should read the second one also.

	// NULL terminate the string
	*pBuffer = '\0';

	return 0;
}

// Function name	: GetZString
// Description	    : 
// Return type		: int 
// Argument         : HANDLE hInput
// Argument         : char *pBuffer
int GetZString(HANDLE hInput, char *pBuffer)
{
	DWORD dwNumRead;
	if (pBuffer == NULL)
		return -1;
	*pBuffer = '\0';

	// Read bytes until reaching a nul character
	pBuffer--;
	do
	{
		pBuffer++;
		if (!ReadFile(hInput, pBuffer, 1, &dwNumRead, NULL))
		{
			*pBuffer = '\0';
			return GetLastError();
		}
		//printf("%c", pBuffer);fflush(stdout);
	} while (*pBuffer != '\0');

	return 0;
}
