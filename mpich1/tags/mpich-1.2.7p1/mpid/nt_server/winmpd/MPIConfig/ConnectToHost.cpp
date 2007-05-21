#include "stdafx.h"
#include "ConnectToHost.h"
#include "mpd.h"
#include "crypt.h"
#include "mpdutil.h"

HANDLE g_hMutex = CreateMutex(NULL, FALSE, NULL);

bool ConnectToHost(const char *host, int port, char *pwd, SOCKET *psock, bool fast/* = false*/)
{
    SOCKET sock;
    char str[100];
    char phrase[100];
    char *result;
    
    strcpy(phrase, pwd);
    
    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	printf("easy_create failed: %d\n", WSAGetLastError());fflush(stdout);
	return false;
    }
    //printf("connecting to %s:%d\n", host, arg->port);fflush(stdout);
    //if (easy_connect_timeout(sock, (char*)host, port, 10) == SOCKET_ERROR)
    if (fast)
    {
	if (easy_connect_quick(sock, (char*)host, port) == SOCKET_ERROR)
	{
	    printf("easy_connect failed: %d\n", WSAGetLastError());fflush(stdout);
	    easy_closesocket(sock);
	    return false;
	}
    }
    else
    {
	if (easy_connect_timeout(sock, (char*)host, port, MPD_DEFAULT_TIMEOUT) == SOCKET_ERROR)
	{
	    printf("easy_connect failed: %d\n", WSAGetLastError());fflush(stdout);
	    easy_closesocket(sock);
	    return false;
	}
    }
    if (!ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	printf("reading prepend string failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    strcat(phrase, str);
    WaitForSingleObject(g_hMutex, INFINITE);
    result = crypt(phrase, MPD_SALT_VALUE);
    strcpy(str, result);
    ReleaseMutex(g_hMutex);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	printf("WriteString of the crypt string failed: %d\n", WSAGetLastError());fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    if (!ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	printf("reading authentication result failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    if (strcmp(str, "SUCCESS"))
    {
	printf("authentication request failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	printf("WriteString failed after attempting passphrase authentication: %d\n", WSAGetLastError());fflush(stdout);
	easy_closesocket(sock);
	return false;
    }
    //printf("connected\n");fflush(stdout);
    *psock = sock;
    return true;
}
