#include "mpdimpl.h"
#include "GetOpt.h"
#include "Translate_Error.h"

void ConnectAndRestart(int *argc, char ***argv, char *host)
{
    SOCKET sock;
    char str[CONSOLE_STR_LENGTH+1];
    char *result;
    int error;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+1];
    int port = -1;
    bool bAskPwd;

    easy_socket_init();
    GetOpt(*argc, *argv, "-port", &port);
    bAskPwd = GetOpt(*argc, *argv, "-getphrase");
    GetOpt(*argc, *argv, "-phrase", phrase);

    ParseRegistry(false);
    if (host == NULL || host[0] == '\0')
	host = g_pszHost;
    if (port == -1)
	port = g_nPort;
    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	err_printf("easy_create failed: %d\n%s\n", error, str);
	return;
    }
    if (bAskPwd || !ReadMPDRegistry("phrase", phrase, false))
    {
	printf("please input the passphrase: ");fflush(stdout);
	HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD dwMode;
	if (!GetConsoleMode(hStdin, &dwMode))
		dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
	SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
	gets(phrase);
	SetConsoleMode(hStdin, dwMode);
	printf("\n");fflush(stdout);
    }
    dbg_printf("connecting to %s:%d\n", host, port);
    if (easy_connect(sock, host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	err_printf("easy_connect failed: %d\n%s\n", error, str);
	easy_closesocket(sock);
	return;
    }
    if (!ReadString(sock, str))
    {
	err_printf("reading challenge string failed.\n");
	easy_closesocket(sock);
	return;
    }
    if (strlen(phrase) + strlen(str) > MPD_PASSPHRASE_MAX_LENGTH)
    {
	err_printf("unable to process passphrase.\n");
	easy_closesocket(sock);
	return;
    }
    strcat(phrase, str);
    result = crypt(phrase, MPD_SALT_VALUE);
    memset(phrase, 0, strlen(phrase)); // zero out the passphrase
    strcpy(str, result);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	err_printf("WriteString of the encrypted response string failed: %d\n%s\n", error, str);
	easy_closesocket(sock);
	return;
    }
    if (!ReadString(sock, str))
    {
	err_printf("reading authentication result failed.\n");
	easy_closesocket(sock);
	return;
    }
    if (strcmp(str, "SUCCESS"))
    {
	err_printf("host authentication failed.\n");
	easy_closesocket(sock);
	return;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	err_printf("WriteString('console') failed: %d\n%s\n", error, str);
	easy_closesocket(sock);
	return;
    }
    dbg_printf("connected\n");

    // send restart request
    if (WriteString(sock, "restart") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	err_printf("writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	err_printf("%s\n", str);
	return;
    }
    //dbg_printf("waiting for result\n");
    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	dbg_printf("%s\n", str);
    }

    if (WriteString(sock, "done") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	err_printf("WriteString failed: %d\n%s\n", error, str);
    }
    easy_closesocket(sock);

    easy_socket_finalize();
}
