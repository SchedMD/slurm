#include "mpdimpl.h"
#include "Translate_Error.h"

static CRITICAL_SECTION g_hCryptCriticalSection;
static bool g_bCryptFirst = true;

bool GenAuthenticationStrings(char *append, char *crypted)
{
    int stamp;
    char *crypted_internal;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+1];
    char phrase_internal[MPD_PASSPHRASE_MAX_LENGTH+1];

    srand(GetTickCount());
    stamp = rand();

    if (!ReadMPDRegistry("phrase", phrase))
	return false;

    _snprintf(phrase_internal, MPD_PASSPHRASE_MAX_LENGTH, "%s%d", phrase, stamp);
    sprintf(append, "%d", stamp);

    if (g_bCryptFirst) // this is not safe code because two threads can enter this Initialize... block at the same time
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    //dbg_printf("GenAuthenticationStrings: calling crypt on '%s'\n", phrase_internal);
    crypted_internal = crypt(phrase_internal, MPD_SALT_VALUE);
    if (strlen(crypted_internal) > MPD_PASSPHRASE_MAX_LENGTH)
    {
	LeaveCriticalSection(&g_hCryptCriticalSection);
	return false;
    }
    strcpy(crypted, crypted_internal);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    return true;
}

bool AuthenticateAcceptedConnection(MPD_Context **pp)
{
    int ret_val;
    MPD_Context *p;

    if (pp == NULL)
	return false;
    p = *pp;

    // generate the challenge string and the encrypted result
    if (!GenAuthenticationStrings(p->pszOut, p->pszCrypt))
    {
	err_printf("AuthenticateAcceptedConnection: failed to generate the authentication strings\n");
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // write the challenge string
    if (WriteString(p->sock, p->pszOut) == SOCKET_ERROR)
    {
	err_printf("AuthenticateAcceptedConnection: Writing challenge string failed, error %d\n", WSAGetLastError());
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // read the response
    if (!ReadString(p->sock, p->pszIn))
    {
	err_printf("AuthenticateAcceptedConnection: Reading challenge response failed, error %d\n", WSAGetLastError());
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // compare the response with the encrypted result and write success or failure
    if (strcmp(p->pszIn, p->pszCrypt) == 0)
	ret_val = WriteString(p->sock, "SUCCESS");
    else
	ret_val = WriteString(p->sock, "FAIL");
    if (ret_val == SOCKET_ERROR)
    {
	err_printf("AuthenticateAcceptedConnection: Writing authentication result failed, error %d\n", WSAGetLastError());
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // read the type of connection
    if (!ReadString(p->sock, p->pszIn))
    {
	err_printf("AuthenticateAcceptedConnection: Reading the connection type failed, error %d\n", WSAGetLastError());
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // set the state appropriate for the type of connection
    if (stricmp(p->pszIn, "console") == 0)
    {
	dbg_printf("AuthenticateAcceptedConnection: MPD_CONSOLE_SOCKET(%d)\n", p->sock);
	p->nType = MPD_CONSOLE_SOCKET;
	p->nLLState= MPD_READING_CMD;
    }
    else if (strnicmp(p->pszIn, "left ", 5) == 0)
    {
	dbg_printf("AuthenticateAcceptedConnection: MPD_LEFT_SOCKET(%d)\n", p->sock);
	p->nType = MPD_LEFT_SOCKET;
	p->nLLState= MPD_READING_CMD;
	strncpy(p->pszHost, &p->pszIn[5], MAX_HOST_LENGTH);
	p->pszHost[MAX_HOST_LENGTH-1] = '\0';
    }
    else if (strnicmp(p->pszIn, "right ", 6) == 0)
    {
	dbg_printf("AuthenticateAcceptedConnection: MPD_RIGHT_SOCKET(%d)\n", p->sock);
	p->nType = MPD_RIGHT_SOCKET;
	p->nLLState= MPD_READING_CMD;
	strncpy(p->pszHost, &p->pszIn[6], MAX_HOST_LENGTH);
	p->pszHost[MAX_HOST_LENGTH-1] = '\0';
    }
    else
    {
	err_printf("AuthenticateAcceptedConnection: unknown socket type read: '%s'\n", p->pszIn);
	RemoveContext(p);
	*pp = NULL;
	return false;
    }
    p->nState = MPD_IDLE;

    return true;
}

bool AuthenticateConnectedConnection(MPD_Context **pp, char *passphrase/* = MPD_DEFAULT_PASSPHRASE*/)
{
    int error;
    MPD_Context *p;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH];
    char *result;

    if (pp == NULL)
	return false;
    p = *pp;

    strcpy(phrase, passphrase);

    // read the challenge string
    if (!ReadString(p->sock, p->pszIn))
    {
	err_printf("AuthenticateConnectedConnection: Reading challenge string failed, error %d\n", WSAGetLastError());
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // crypt the passphrase + the challenge
    if (strlen(phrase) + strlen(p->pszIn) > MPD_PASSPHRASE_MAX_LENGTH)
    {
	err_printf("AuthenticateConnectedConnection: unable to process passphrase.\n");
	RemoveContext(p);
	*pp = NULL;
	return false;
    }
    strcat(phrase, p->pszIn);
    if (g_bCryptFirst) // this is not safe code because two threads can enter this Initialize... block at the same time
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(p->pszOut, result);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    // write the response
    if (WriteString(p->sock, p->pszOut) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, p->pszOut);
	err_printf("AuthenticateConnectedConnection: WriteString of the encrypted response string failed: %d\n%s\n", error, p->pszOut);
	RemoveContext(p);
	*pp = NULL;
	return false;
    }

    // read the result
    if (!ReadString(p->sock, p->pszIn))
    {
	error = WSAGetLastError();
	Translate_Error(error, p->pszOut);
	err_printf("AuthenticateConnectedConnection: reading authentication result failed: error %d\n%s\n", error, p->pszOut);
	RemoveContext(p);
	*pp = NULL;
	return false;
    }
    if (strcmp(p->pszIn, "SUCCESS"))
    {
	dbg_printf("host authentication failed.\n");
	RemoveContext(p);
	*pp = NULL;
	return false;
    }
    return true;
}
