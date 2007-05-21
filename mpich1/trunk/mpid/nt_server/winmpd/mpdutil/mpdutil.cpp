#include <stdio.h>
#include "mpdutil.h"
#include "mpd.h"
#include "crypt.h"
#include "Translate_Error.h"

#define MAX_FILENAME MAX_PATH * 2

static CRITICAL_SECTION g_hCryptCriticalSection;
static bool g_bCryptFirst = true;

bool TryCreateDir(char *pszFileName, char *pszError)
{
    char pszTemp[MAX_FILENAME];
    char *token, *next_token;
    int error;

    if (pszFileName[1] == ':')
    {
	strncpy(pszTemp, pszFileName, 3);
	pszTemp[3] = '\0';
	//dbg_printf("changing into directory '%s'\n", pszTemp);
	if (!SetCurrentDirectory(pszTemp))
	{
	    sprintf(pszError, "unable to change to '%s' directory", pszTemp);
	    return false;
	}
	strncpy(pszTemp, &pszFileName[3], MAX_FILENAME);
    }
    else
    {
	sprintf(pszError, "full path not provided");
	// full path not provided
	return false;
    }

    token = strtok(pszTemp, "\\/");
    while (token)
    {
	next_token = strtok(NULL, "\\/");
	if (next_token == NULL)
	    return true;
	//dbg_printf("creating directory '%s'\n", token);
	if (!CreateDirectory(token, NULL))
	{
	    error = GetLastError();
	    if (error != ERROR_ALREADY_EXISTS)
	    {
		sprintf(pszError, "unable to create directory '%s', error %d\n", token, error);
		return false;
	    }
	}
	SetCurrentDirectory(token);
	token = next_token;
    }
    strcpy(pszError, "unknown error");
    return false;
}

#define MPD_CONNECT_READ_TIMEOUT 10

int ConnectToMPD(const char *host, int port, const char *inphrase, SOCKET *psock)
{
    SOCKET sock;
    char str[512];
    char err_msg[1024];
    char *result;
    int error;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    BOOL b;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+20];

    if (host == NULL || host[0] == '\0' || port < 1 || inphrase == NULL || psock == NULL)
	return -1;

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPD(%s:%d): easy_create failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	return error;
    }
#ifdef USE_LINGER_SOCKOPT
    linger.l_onoff = 1;
    linger.l_linger = 60;
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPD(%s:%d): setsockopt failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
#endif
    b = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
    //printf("connecting to %s:%d\n", host, port);fflush(stdout);
    if (easy_connect(sock, (char*)host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPD(%s:%d): easy_connect failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	if (error == WSAEINVAL)
	{
	    printf("The hostname is probably invalid.\n");
	}
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	printf("Error: ConnectToMPD(%s:%d): reading prepend string failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    //strcat(phrase, str);
    _snprintf(phrase, MPD_PASSPHRASE_MAX_LENGTH+20, "%s%s", inphrase, str);

    if (g_bCryptFirst) // this is not safe code because two threads can enter this Initialize... block at the same time
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(str, result);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    memset(phrase, 0, MPD_PASSPHRASE_MAX_LENGTH); // zero out local copy of the passphrase
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPD(%s:%d): WriteString of the crypt string failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	printf("Error: ConnectToMPD(%s:%d): reading authentication result failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    if (strcmp(str, "SUCCESS"))
    {
	printf("Error: ConnectToMPD(%s:%d): authentication request failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPD(%s:%d): WriteString failed after attempting passphrase authentication: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    //printf("connected to %s\n", host);fflush(stdout);
    *psock = sock;
    return 0;
}

int ConnectToMPDquick(const char *host, int port, const char *inphrase, SOCKET *psock)
{
    SOCKET sock;
    char str[512];
    char err_msg[1024];
    char *result;
    int error;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    BOOL b;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+20];

    if (host == NULL || host[0] == '\0' || port < 1 || inphrase == NULL || psock == NULL)
	return -1;

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDquick(%s:%d): easy_create failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	return error;
    }
#ifdef USE_LINGER_SOCKOPT
    linger.l_onoff = 1;
    linger.l_linger = 60;
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDquick(%s:%d): setsockopt failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
#endif
    b = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
    //printf("connecting to %s:%d\n", host, port);fflush(stdout);
    if (easy_connect_quick(sock, (char*)host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDquick(%s:%d): easy_connect_quick failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	if (error == WSAEINVAL)
	{
	    printf("The hostname is probably invalid.\n");
	}
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	printf("Error: ConnectToMPDquick(%s:%d): reading prepend string failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    //strcat(phrase, str);
    _snprintf(phrase, MPD_PASSPHRASE_MAX_LENGTH+20, "%s%s", inphrase, str);

    if (g_bCryptFirst)
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(str, result);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    memset(phrase, 0, MPD_PASSPHRASE_MAX_LENGTH); // zero out local copy of the passphrase
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDquick(%s:%d): WriteString of the crypt string failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	printf("Error: ConnectToMPDquick(%s:%d): reading authentication result failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    if (strcmp(str, "SUCCESS"))
    {
	printf("Error: ConnectToMPDquick(%s:%d): authentication request failed.\n", host, port);
	fflush(stdout);
	easy_closesocket(sock);
	return -1;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDquick(%s:%d): WriteString failed after attempting passphrase authentication: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	printf("%s\n", err_msg);
	fflush(stdout);
	easy_closesocket(sock);
	return error;
    }
    //printf("connected to %s\n", host);fflush(stdout);
    *psock = sock;
    return 0;
}

int ConnectToMPDReport(const char *host, int port, const char *inphrase, SOCKET *psock, char *err_msg)
{
    SOCKET sock;
    char str[512];
    char *result;
    int error;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    BOOL b;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+20];

    if (host == NULL || host[0] == '\0' || port < 1 || inphrase == NULL || psock == NULL)
    {
	strcpy(err_msg, "Error: ConnectToMPDReport: Invalid argument");
	return -1;
    }

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDReport(%s:%d): easy_create failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	return error;
    }
#ifdef USE_LINGER_SOCKOPT
    linger.l_onoff = 1;
    linger.l_linger = 60;
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDReport(%s:%d): setsockopt failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
#endif
    b = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
    //printf("connecting to %s:%d\n", host, port);fflush(stdout);
    if (easy_connect(sock, (char*)host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDReport(%s:%d): easy_connect failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	if (error == WSAEINVAL)
	{
	    strcat(err_msg, ".  The hostname is probably invalid.");
	}
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	sprintf(err_msg, "Error: ConnectToMPDReport(%s:%d): reading prepend string failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    //strcat(phrase, str);
    _snprintf(phrase, MPD_PASSPHRASE_MAX_LENGTH+20, "%s%s", inphrase, str);

    if (g_bCryptFirst)
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(str, result);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    memset(phrase, 0, MPD_PASSPHRASE_MAX_LENGTH); // zero out local copy of the passphrase
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDReport(%s:%d): WriteString of the crypt string failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	sprintf(err_msg, "Error: ConnectToMPDReport(%s:%d): reading authentication result failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    if (strcmp(str, "SUCCESS"))
    {
	sprintf(err_msg, "Error: ConnectToMPDReport(%s:%d): authentication request failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error: ConnectToMPDReport(%s:%d): WriteString failed after attempting passphrase authentication: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
    strcpy(err_msg, "ERROR_SUCCESS");
    //printf("connected to %s\n", host);fflush(stdout);
    *psock = sock;
    return 0;
}

int ConnectToMPDquickReport(const char *host, int port, const char *inphrase, SOCKET *psock, char *err_msg)
{
    SOCKET sock;
    char str[512];
    char *result;
    int error;
#ifdef USE_LINGER_SOCKOPT
    struct linger linger;
#endif
    BOOL b;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+20];

    if (host == NULL || host[0] == '\0' || port < 1 || inphrase == NULL || psock == NULL)
    {
	strcpy(err_msg, "Error: ConnectToMPDquickReport: Invalid argument");
	return -1;
    }

    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error:ConnectToMPDquickReport(%s:%d): easy_create failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	return error;
    }
#ifdef USE_LINGER_SOCKOPT
    linger.l_onoff = 1;
    linger.l_linger = 60;
    if (setsockopt(sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger)) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error:ConnectToMPDquickReport(%s:%d): setsockopt failed: %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
#endif
    b = TRUE;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, (char*)&b, sizeof(BOOL));
    //printf("connecting to %s:%d\n", host, port);fflush(stdout);
    if (easy_connect_quick(sock, (char*)host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error:ConnectToMPDquickReport(%s:%d): easy_connect failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	if (error == WSAEINVAL)
	{
	    strcat(err_msg, ".  The hostname is probably invalid.");
	}
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	sprintf(err_msg, "Error:ConnectToMPDquickReport(%s:%d): reading challenge prepend string failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    //strcat(phrase, str);
    _snprintf(phrase, MPD_PASSPHRASE_MAX_LENGTH+20, "%s%s", inphrase, str);

    if (g_bCryptFirst)
    {
	InitializeCriticalSection(&g_hCryptCriticalSection);
	g_bCryptFirst = false;
    }
    EnterCriticalSection(&g_hCryptCriticalSection);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(str, result);
    LeaveCriticalSection(&g_hCryptCriticalSection);

    memset(phrase, 0, MPD_PASSPHRASE_MAX_LENGTH); // zero out local copy of the passphrase
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error:ConnectToMPDquickReport(%s:%d): WriteString of the crypt string failed: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
    if (!ReadStringTimeout(sock, str, MPD_CONNECT_READ_TIMEOUT))
    {
	sprintf(err_msg, "Error:ConnectToMPDquickReport(%s:%d): reading mpd authentication result failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    if (strcmp(str, "SUCCESS"))
    {
	sprintf(err_msg, "Error:ConnectToMPDquickReport(%s:%d): mpd authentication request failed.", host, port);
	easy_closesocket(sock);
	return -1;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(str, "Error:ConnectToMPDquickReport(%s:%d): WriteString failed after attempting passphrase authentication: error %d, ", host, port, error);
	Translate_Error(error, err_msg, str);
	easy_closesocket(sock);
	return error;
    }
    strcpy(err_msg, "ERROR_SUCCESS");
    //printf("connected to %s\n", host);fflush(stdout);
    *psock = sock;
    return 0;
}

void MakeLoop(SOCKET *psockRead, SOCKET *psockWrite)
{
    SOCKET sock;
    char host[100];
    int port;
    static char ipstr[20] = ""; /* cached local ip string */

    // Create a listener
    if (easy_create(&sock, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }
    listen(sock, 5);
    easy_get_sock_info(sock, host, &port);
    if (ipstr[0] == '\0')
    {
	easy_get_ip_string(host, ipstr);
    }
    
    // Connect to myself
    if (easy_create(psockWrite, ADDR_ANY, INADDR_ANY) == SOCKET_ERROR)
    {
	easy_closesocket(sock);
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }
    if (easy_connect(*psockWrite, ipstr, port) == SOCKET_ERROR)
    {
	easy_closesocket(*psockWrite);
	easy_closesocket(sock);
	*psockRead = INVALID_SOCKET;
	*psockWrite = INVALID_SOCKET;
	return;
    }

    // Accept the connection from myself
    *psockRead = easy_accept(sock);

    easy_closesocket(sock);
}
