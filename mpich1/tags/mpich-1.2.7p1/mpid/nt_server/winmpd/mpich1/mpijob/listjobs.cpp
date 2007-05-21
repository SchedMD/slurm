#include "mpijob.h"
#include "Translate_Error.h"

void GetAndPrintState(SOCKET sock, char *dbname)
{
    char str[256];
    int error;

    sprintf(str, "dbget %s:state", dbname);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	printf("writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	printf("%s\n", str);
	fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    printf("unexpected error reading the next job\n");
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	printf("%s\n", str);
    }
    else
    {
	printf("Unable to read the job state.\n");
	fflush(stdout);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return;
    }
}

void ListJobs(char *host, int port, char *altphrase)
{
    SOCKET sock;
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];
    char localhost[100];

    if (host == NULL)
    {
	gethostname(localhost, 100);
	host = localhost;
    }

    if (ConnectToMPD(host, port, (altphrase == NULL) ? MPD_DEFAULT_PASSPHRASE : altphrase, &sock) != 0)
    {
	printf("Unable to connect to the mpd on %s\n", host);
	fflush(stdout);
	return;
    }

    printf("Jobs on %s:\n", host);
    //printf("start time : user@jobid\n");
    printf("yyyy.mm.dd<hh .mm .ss > : user@jobid : state\n");
    printf("--------------------------------------------\n");
    fflush(stdout);
    strcpy(str, "dbfirst jobs");
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	printf("writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	printf("%s\n", str);
	fflush(stdout);
	easy_closesocket(sock);
	return;
    }
    if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    printf("no jobs on %s\n", host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    printf("no jobs on %s\n", host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
	}
	GetKeyAndValue(str, key, value);
	printf("%s : %s : ", key, value);
	fflush(stdout);
	GetAndPrintState(sock, strstr(value, "@")+1);
    }
    else
    {
	printf("Unable to read the jobs on %s.\n", host);
	fflush(stdout);
	WriteString(sock, "done");
	easy_closesocket(sock);
	return;
    }

    while (true)
    {
	strcpy(str, "dbnext jobs");
	if (WriteString(sock, str) == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    printf("writing '%s' failed, %d\n", str, error);
	    Translate_Error(error, str);
	    printf("%s\n", str);
	    fflush(stdout);
	    easy_closesocket(sock);
	    return;
	}
	if (ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
	{
	    if (strcmp(str, "DBS_FAIL") == 0)
	    {
		printf("unexpected error reading the next job\n");
		fflush(stdout);
		WriteString(sock, "done");
		easy_closesocket(sock);
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    printf("%s : %s : ", key, value);
	    GetAndPrintState(sock, strstr(value, "@")+1);
	}
	else
	{
	    printf("Unable to read the jobs on %s.\n", host);
	    fflush(stdout);
	    WriteString(sock, "done");
	    easy_closesocket(sock);
	    return;
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
