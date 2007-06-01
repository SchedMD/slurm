#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "mpd.h"
#include "mpdutil.h"
#include "..\Common\MPICH_pwd.h"
#include "GetOpt.h"

void DeleteCachedPassword()
{
    char szKey[256];
    strcpy(szKey, MPICHKEY"\\cache");

    RegDeleteKey(HKEY_CURRENT_USER, szKey);
}

void main(int argc, char *argv[])
{
	char ch=0, account[100]="", password[100]="", confirm[100]="";
	int index = 0;
	bool done, persistent = true;
	char pszHost[100], pszPassPhrase[100] = MPD_DEFAULT_PASSPHRASE;
	char pszPort[100];
	char pszStr[1024];
	char pszErrMsg[1024];
	char *pszEncoded;
	int nPort = MPD_DEFAULT_PORT;
	SOCKET sock;
	int error;
	bool bUseCache = true;
	bool bNoSpecificError = false;

	if ((argc > 1) && (stricmp(argv[1], "-remove") == 0))
	{
		if (DeleteCurrentPasswordRegistryEntry())
		{
			DeleteCachedPassword();
			printf("Account and password removed from the Registry.\n");
		}
		else
			printf("ERROR: Unable to remove the encrypted password.\n");
		return;
	}

	if (GetOpt(argc, argv, "-validate"))
	{
	    if (SetupCryptoClient())
	    {
		if (ReadPasswordFromRegistry(account, password))
		{
		    if (GetOpt(argc, argv, "-nodetails"))
			bNoSpecificError = true;
		    if (GetOpt(argc, argv, "-nocache"))
			bUseCache = false;
		    if (!GetOpt(argc, argv, "-host", pszHost))
		    {
			DWORD len = 100;
			GetComputerName(pszHost, &len);
		    }
		    if (GetOpt(argc, argv, "-port", pszPort))
			nPort = atoi(pszPort);
		    GetOpt(argc, argv, "-phrase", pszPassPhrase);
		    easy_socket_init();
		    if ((error = ConnectToMPDReport(pszHost, nPort, pszPassPhrase, &sock, pszErrMsg)) == 0)
		    {
			pszEncoded = EncodePassword(password);
			sprintf(pszStr, "validate a=%s p=%s c=%s", account, pszEncoded, bUseCache ? "yes" : "no");
			WriteString(sock, pszStr);
			strcpy(pszStr, "FAIL");
			ReadStringTimeout(sock, pszStr, 20);
			printf("%s\n", pszStr);fflush(stdout);
			easy_socket_finalize();
			ExitProcess(0);
		    }
		    if (bNoSpecificError)
			printf("ERROR: Unable to connect to the mpd.\n");
		    else
		    {
			printf("ERROR: Unable to connect to the mpd on host <%s>.\n", pszHost);
			printf("%s\n", pszErrMsg);
		    }
		    fflush(stdout);
		    easy_socket_finalize();
		}
		else
		{
		    printf("FAIL: Unable to read the credentials from the registry.\n");fflush(stdout);
		}
	    }
	    else
	    {
		printf("FAIL: Unable to setup the encryption service.\n");fflush(stdout);
	    }
	    ExitProcess(0);
	}

	do
	{
		printf("account: ");
		fflush(stdout);
		gets(account);
	} while (strlen(account) == 0);

	done = false;
	do 
	{
		printf("password: ");
		fflush(stdout);
		ch = getch();
		index = 0;
		while (ch != 13)//'\r')
		{
			password[index] = ch;
			index++;
			ch = getch();
		}
		password[index] = '\0';
		
		printf("\nconfirm: ");
		fflush(stdout);
		ch =  getch();
		index = 0;
		while (ch != 13)//'\r')
		{
			confirm[index] = ch;
			index++;
			ch = getch();
		}
		confirm[index] = '\0';

		if (strcmp(password, confirm) == 0)
			done = true;
		else
		{
			printf("\nPasswords don't match.\n");
			fflush(stdout);
		}
	} while (!done);
	printf("\n");
	fflush(stdout);

	confirm[0] = '\0';
	while (confirm[0] != 'y' && confirm[0] != 'Y' &&
		confirm[0] != 'n' && confirm[0] != 'N')
	{
		printf("Do you want this action to be persistent (y/n)? ");
		fflush(stdout);
		gets(confirm);
	}

	if (confirm[0] == 'y' || confirm[0] == 'Y')
		persistent = true;
	else
		persistent =  false;

	if (SetupCryptoClient())
	{
		if (SavePasswordToRegistry(
			account, 
			password, 
			persistent))
		{
			printf("Password encrypted into the Registry.\n");
			DeleteCachedPassword();
		}
		else
		{
			printf("Error: Unable to save encrypted password.\n");
		}
	}
	else
	{
		printf("Error: Unable to setup the encryption service.\n");
	}
}
