#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "..\Common\MPICH_pwd.h"

void main(int argc, char *argv[])
{
	char ch=0, account[100]="", password[100]="", confirm[100]="";
	int index = 0;
	bool done, persistent = true;

	if ((argc > 1) && (stricmp(argv[1], "-remove") == 0))
	{
		if (DeleteCurrentPasswordRegistryEntry())
			printf("Account and password removed from the Registry.\n");
		else
			printf("Error: Unable to remove the encrypted password.\n");
		return;
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
		password[index] = 0;
		
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
