#include "mpdimpl.h"
#include <stdio.h>
#include "GetStringOpt.h"
#include "Translate_Error.h"
#include "mpdutil.h"
#include <conio.h> /* getch */

static void GetPassword(char *question, char *account, char *password)
{
    if (question != NULL)
	printf(question);
    else
	printf("password for %s: ", account);
    fflush(stdout);
    
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwMode;
    if (!GetConsoleMode(hStdin, &dwMode))
	dwMode = ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT | ENABLE_MOUSE_INPUT;
    SetConsoleMode(hStdin, dwMode & ~ENABLE_ECHO_INPUT);
    gets(password);
    SetConsoleMode(hStdin, dwMode);
    
    printf("\n");
    fflush(stdout);
}

void DoConsole(char *host, int port, bool bAskPwd, char *altphrase)
{
    SOCKET sock;
    char phrase[MPD_PASSPHRASE_MAX_LENGTH+1];
    char str[CONSOLE_STR_LENGTH+1];
    char *result;
    int error;

    easy_socket_init();
    ParseRegistry(false);
    if (host == NULL || host[0] == '\0')
	host = g_pszHost;
    if (port == -1)
	port = g_nPort;
    if (easy_create(&sock, 0, INADDR_ANY) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("easy_create failed: %d\n%s\n", error, str);
	fflush(stdout);
	return;
    }
    if (altphrase != NULL)
    {
	strncpy(phrase, altphrase, MPD_PASSPHRASE_MAX_LENGTH);
	phrase[MPD_PASSPHRASE_MAX_LENGTH] = '\0';
    }
    else if (bAskPwd || !ReadMPDRegistry("phrase", phrase, false))
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
    printf("connecting to %s:%d\n", host, port);fflush(stdout);
    if (easy_connect(sock, host, port) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("easy_connect failed: %d\n%s\n", error, str);fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (!ReadString(sock, str))
    {
	printf("reading challenge string failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (strlen(phrase) + strlen(str) > MPD_PASSPHRASE_MAX_LENGTH)
    {
	printf("unable to process passphrase.\n");fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    strcat(phrase, str);
    result = crypt(phrase, MPD_SALT_VALUE);
    memset(phrase, 0, strlen(phrase)); // zero out the passphrase
    if (altphrase != NULL)
	memset(altphrase, 0, strlen(altphrase)); // zero out the passphrase
    strcpy(str, result);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("WriteString of the encrypted response string failed: %d\n%s\n", error, str);fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (!ReadString(sock, str))
    {
	printf("reading authentication result failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (strcmp(str, "SUCCESS"))
    {
	printf("host authentication failed.\n");fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (WriteString(sock, "console") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("WriteString('console') failed: %d\n%s\n", error, str);fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    printf("connected\n");fflush(stdout);
    while (gets(str))
    {
	if ((strnicmp(str, "getpid ", 7) == 0) || 
	    (strnicmp(str, "geterror ", 9) == 0) ||
	    (strnicmp(str, "getexitcode ", 12) == 0) ||
	    (strnicmp(str, "getexitcodewait ", 16) == 0) ||
	    (strnicmp(str, "getexittime ", 12) == 0) ||
	    (stricmp(str, "version") == 0) ||
	    (stricmp(str, "mpich version") == 0) ||
	    (stricmp(str, "config") == 0) ||
	    (strnicmp(str, "dbput ", 6) == 0) ||
	    (strnicmp(str, "dbget ", 6) == 0) ||
	    (stricmp(str, "dbcreate") == 0) ||
	    (strnicmp(str, "dbcreate ", 9) == 0) ||
	    (strnicmp(str, "dbdestroy ", 10) == 0) ||
	    (strnicmp(str, "dbfirst ", 8) == 0) ||
	    (strnicmp(str, "dbnext ", 7) == 0) ||
	    (stricmp(str, "dbfirstdb") == 0) ||
	    (stricmp(str, "dbnextdb") == 0) ||
	    (strnicmp(str, "dbdelete ", 9) == 0) ||
	    (stricmp(str, "ps") == 0) ||
	    (stricmp(str, "forwarders") == 0) ||
	    (strnicmp(str, "createtmpfile ", 14) == 0) ||
	    (strnicmp(str, "deletetmpfile ", 14) == 0) ||
	    (strnicmp(str, "mpich1readint ", 14) == 0) ||
	    (strnicmp(str, "freeprocess ", 12) == 0) ||
	    (strnicmp(str, "lget ", 5) == 0) ||
	    (strnicmp(str, "freecached", 10) == 0) ||
	    (strnicmp(str, "setdbgoutput ", 13) == 0) ||
	    (strnicmp(str, "canceldbgoutput", 15) == 0) ||
	    (stricmp(str, "clrmpduser") == 0) ||
	    (stricmp(str, "enablempduser") == 0) ||
	    (stricmp(str, "disablempduser") == 0))
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("timeout waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "launch ", 7) == 0)
	{
	    char pszPassword[100];
	    if (GetStringOpt(str, "p", pszPassword))
	    {
		char pszStrTemp[300] = "";
		char *pszEncoded, *pStr;
		unsigned int i;
		pszEncoded = EncodePassword(pszPassword);
		if (pszEncoded != NULL)
		{
		    _snprintf(pszStrTemp, 300, " p=%s", pszEncoded);
		    free(pszEncoded);
		}
		/* erase the original password */
		pStr = strstr(str, "p=");
		pStr = strstr(pStr, pszPassword);
		for (i=0; i<strlen(pszPassword); i++)
		    pStr[i] = ' ';
		while (*pStr != '=')
		    pStr--;
		*pStr = ' ';
		while (*pStr != 'p')
		    pStr--;
		*pStr = ' ';
		/* append the encoded password */
		strcat(str, pszStrTemp);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("timeout waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if ((strnicmp(str, "setmpduser ", 11) == 0) || (stricmp(str, "setmpduser") == 0))
	{
	    // get the account
	    char pszAccount[100];
	    if (!GetStringOpt(str, "a", pszAccount))
	    {
		printf("account: ");
		fflush(stdout);
		gets(pszAccount);
	    }
	    // get the password
	    char pszPassword[100];
	    if (!GetStringOpt(str, "p", pszPassword))
	    {
		char ch;
		int index;

		printf("password: ");
		fflush(stdout);
		ch = getch();
		index = 0;
		while (ch != 13)//'\r')
		{
			pszPassword[index] = ch;
			index++;
			ch = getch();
		}
		pszPassword[index] = '\0';
		printf("\n");
	    }
	    // encode the password
	    char pszStrTemp[300] = "";
	    char *pszEncoded;
	    pszEncoded = EncodePassword(pszPassword);
	    if (pszEncoded != NULL)
	    {
		// create the command
		sprintf(str, "setmpduser a=%s p=%s", pszAccount, pszEncoded);
		free(pszEncoded);

		if (WriteString(sock, str) == SOCKET_ERROR)
		{
		    error = WSAGetLastError();
		    printf("writing '%s' failed, %d\n", str, error);
		    Translate_Error(error, str);
		    printf("%s\n", str);
		    fflush(stdout);
		    break;
		}
		if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
		{
		    printf("%s\n", str);fflush(stdout);
		}
		else
		{
		    printf("timeout waiting for result to return.\n");fflush(stdout);
		}
	    }
	    else
	    {
		printf("FAIL - unable to encode the password for transmission.\n");
	    }
	}
	else if (strnicmp(str, "validate ", 9) == 0)
	{
	    char pszPassword[100];
	    if (GetStringOpt(str, "p", pszPassword))
	    {
		char pszStrTemp[300] = "";
		char *pszEncoded, *pStr;
		unsigned int i;
		pszEncoded = EncodePassword(pszPassword);
		if (pszEncoded != NULL)
		{
		    _snprintf(pszStrTemp, 300, " p=%s", pszEncoded);
		    free(pszEncoded);
		}
		/* erase the original password */
		pStr = strstr(str, "p=");
		pStr = strstr(pStr, pszPassword);
		for (i=0; i<strlen(pszPassword); i++)
		    pStr[i] = ' ';
		while (*pStr != '=')
		    pStr--;
		*pStr = ' ';
		while (*pStr != 'p')
		    pStr--;
		*pStr = ' ';
		/* append the encoded password */
		strcat(str, pszStrTemp);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("timeout waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "barrier ", 8) == 0)
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadString(sock, str))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("error waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if (stricmp(str, "hosts") == 0)
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		Translate_Error(error, str);
		printf("writing hosts request failed, %d\n%s\n", error, str);fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		char *p = strstr(str, "result=");
		if (p != NULL)
		{
		    printf("%s\n", &p[7]);fflush(stdout);
		}
		else
		{
		    printf("%s\n", str);fflush(stdout);
		}
	    }
	    else
	    {
		printf("timeout waiting for result to return\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "next ", 5) == 0)
	{
	    int n = atoi(&str[5]);
	    if ((n < 1) || (n > 16384))
	    {
		printf("invalid number of hosts requested\n");fflush(stdout);
		continue;
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		Translate_Error(error, str);
		printf("writing 'next' command failed, %d\n%s\n", error, str);fflush(stdout);
		break;
	    }
	    for (int i=0; i<n; i++)
	    {
		if (!ReadString(sock, str))
		{
		    printf("Error reading host name\n");
		    break;
		}
		printf("%s\n", str);
	    }
	    fflush(stdout);
	}
	else if (strnicmp(str, "getexitcodewaitmultiple ", 24) == 0)
	{
	    int n = 0;
	    char str2[100];
	    char *token = strtok(&str[24], ",");
	    while (token != NULL)
	    {
		_snprintf(str2, 100, "getexitcodewait %s", token);
		if (WriteString(sock, str2) == SOCKET_ERROR)
		{
		    error = WSAGetLastError();
		    Translate_Error(error, str);
		    printf("writing 'getexitcodewaitmultiple' failed, %d\n%s\n", error, str);fflush(stdout);
		    n = 0;
		    break;
		}
		n++;
		token = strtok(NULL, ",");
	    }
	    for (int i=0; i<n; i++)
	    {
		if (!ReadString(sock, str))
		{
		    error = WSAGetLastError();
		    Translate_Error(error, str);
		    printf("reading exitcode failed, %d\n%s\n", error, str);fflush(stdout);
		    break;
		}
		printf("%s\n", str);
	    }
	}
	else if ((stricmp(str, "extract") == 0) ||
	    (strnicmp(str, "insert ", 7) == 0) ||
	    (strnicmp(str, "set ", 4) == 0) ||
	    (strnicmp(str, "lset ", 5) == 0) ||
	    (strnicmp(str, "ldelete ", 8) == 0) ||
	    (strnicmp(str, "update ", 7) == 0) ||
	    (strnicmp(str, "stopforwarder ", 14) == 0) ||
	    (stricmp(str, "killforwarders") == 0))
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' request failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	}
	else if ((stricmp(str, "exit") == 0) || 
	    (stricmp(str, "quit") == 0) || 
	    (stricmp(str, "done") == 0))
	{
	    break;
	}
	else if (stricmp(str, "shutdown") == 0)
	{
	    if (WriteString(sock, "shutdown") == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		Translate_Error(error, str);
		printf("writing shutdown request failed, %d\n%s", error, str);fflush(stdout);
	    }
	    break;
	}
	else if ((stricmp(str, "exitall") == 0) || (stricmp(str, "shutdownall") == 0))
	{
	    if (WriteString(sock, "exitall") == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing %s request failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
	    }
	    break;
	}
	else if ((strnicmp(str, "kill ", 5) == 0) || (stricmp(str, "killall") == 0))
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' request failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
	    }
	}
	else if (strnicmp(str, "fileinit ", 9) == 0)
	{
	    char pszPassword[100], *pszEncoded;
	    if (!GetStringOpt(str, "password", pszPassword))
	    {
		char pszAccount[100];
		if (!GetStringOpt(str, "account", pszAccount))
		{
		    printf("no account and password specified\n");
		    fflush(stdout);
		    break;
		}
		GetPassword(NULL, pszAccount, pszPassword);
		pszEncoded = EncodePassword(pszPassword);
		_snprintf(str, CONSOLE_STR_LENGTH, "fileinit account=%s password=%s", pszAccount, pszEncoded);
		if (pszEncoded != NULL) free(pszEncoded);
	    }
	    else
	    {
		char pszAccount[100];
		if (!GetStringOpt(str, "account", pszAccount))
		{
		    printf("password but no account specified\n");
		    fflush(stdout);
		    break;
		}
		pszEncoded = EncodePassword(pszPassword);
		_snprintf(str, CONSOLE_STR_LENGTH, "fileinit account=%s password=%s", pszAccount, pszEncoded);
		if (pszEncoded != NULL) free(pszEncoded);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' request failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	}
	else if (strnicmp(str, "map ", 4) == 0)
	{
	    char pszPassword[100];
	    char pszAccount[100];
	    char pszStrTemp[300];
	    char *pszEncoded, *pStr;
	    unsigned int i;
	    if (!GetStringOpt(str, "password", pszPassword))
	    {
		if (!GetStringOpt(str, "account", pszAccount))
		{
		    printf("no account and password specified\n");
		    fflush(stdout);
		    break;
		}
		GetPassword(NULL, pszAccount, pszPassword);
		pszEncoded = EncodePassword(pszPassword);
		if (pszEncoded != NULL)
		{
		    _snprintf(pszStrTemp, 300, " account=%s password=%s", pszAccount, pszEncoded);
		    free(pszEncoded);
		    strcat(str, pszStrTemp);
		}
	    }
	    else
	    {
		if (!GetStringOpt(str, "account", pszAccount))
		{
		    printf("password but no account specified\n");
		    fflush(stdout);
		    break;
		}
		pszEncoded = EncodePassword(pszPassword);
		if (pszEncoded != NULL)
		{
		    _snprintf(pszStrTemp, 300, " password=%s", pszEncoded);
		    free(pszEncoded);
		}
		else
		    pszStrTemp[0] = '\0';
		/* erase the original password */
		pStr = strstr(str, "password");
		while (*pStr != '=')
		{
		    *pStr = ' ';
		    pStr++;
		}
		*pStr = ' ';
		pStr = strstr(pStr, pszPassword);
		for (i=0; i<strlen(pszPassword); i++)
		    pStr[i] = ' ';
		/* append the encoded password */
		strcat(str, pszStrTemp);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing map command failed, %d\n", error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT*2)) // logon requests can take a long time
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("timeout waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "unmap ", 6) == 0)
	{
	    char pszDrive[10];
	    if (!GetStringOpt(str, "drive", pszDrive))
	    {
		char pszStrTemp[40];
		_snprintf(pszStrTemp, 40, "unmap drive=%s", &str[6]);
		pszStrTemp[39] = '\0';
		strcpy(str, pszStrTemp);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing unmap command failed, %d\n", error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    else
	    {
		printf("timeout waiting for result to return.\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "putfile ", 8) == 0)
	{
	    if (PutFile(sock, &str[8]))
	    {
		printf("SUCCESS\n");fflush(stdout);
	    }
	}
	else if (strnicmp(str, "getfile ", 8) == 0)
	{
	    GetFile(sock, &str[8]);
	}
	else if (strnicmp(str, "getdir ", 7) == 0)
	{
	    GetDirectoryContents(sock, str);
	}
	else if (stricmp(str, "restart") == 0)
	{
	    //dbg_printf("writing 'restart'\n");fflush(stdout);
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    //dbg_printf("waiting for result\n");fflush(stdout);
	    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s\n", str);fflush(stdout);
	    }
	    break;
	}
	else if (stricmp(str, "print") == 0)
	{
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringMax(sock, str, CONSOLE_STR_LENGTH))
	    {
		printf("%s", str);fflush(stdout);
	    }
	    else
	    {
		printf("reading result failed\n");fflush(stdout);
		break;
	    }
	}
	else if (stricmp(str, "stat") == 0)
	{
	    strcpy(str, "stat param=help");
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringMaxTimeout(sock, str, CONSOLE_STR_LENGTH, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s", str);fflush(stdout);
	    }
	    else
	    {
		printf("reading result failed\n");fflush(stdout);
		break;
	    }
	}
	else if (strnicmp(str, "stat ", 5) == 0)
	{
	    char pszParam[100];
	    if (!GetStringOpt(str, "param", pszParam))
	    {
		char pszStrTemp[100];
		_snprintf(pszStrTemp, 100, "stat param=%s", &str[5]);
		pszStrTemp[99] = '\0';
		strcpy(str, pszStrTemp);
	    }
	    if (WriteString(sock, str) == SOCKET_ERROR)
	    {
		error = WSAGetLastError();
		printf("writing '%s' failed, %d\n", str, error);
		Translate_Error(error, str);
		printf("%s\n", str);
		fflush(stdout);
		break;
	    }
	    if (ReadStringMaxTimeout(sock, str, CONSOLE_STR_LENGTH, MPD_DEFAULT_TIMEOUT))
	    {
		printf("%s", str);fflush(stdout);
	    }
	    else
	    {
		printf("reading result failed\n");fflush(stdout);
		break;
	    }
	}
	else
	{
	    printf("unknown command\n");fflush(stdout);
	}
    }
    if (WriteString(sock, "done") == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	Translate_Error(error, str);
	printf("WriteString failed: %d\n%s\n", error, str);
	fflush(stdout);
    }
    easy_closesocket(sock);
}
