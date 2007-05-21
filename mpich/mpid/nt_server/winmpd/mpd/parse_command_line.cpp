#include "mpdimpl.h"
#include "mpdutil.h"
#include "GetOpt.h"
#include "Translate_Error.h"
#include "Service.h"

static bool GetNextHost(FILE *fin, char *pszHost)
{
    char buffer[1024] = "";
    char *pChar, *pChar2;

    while (fgets(buffer, 1024, fin))
    {
	pChar = buffer;
	
	// Advance over white space
	while (*pChar != '\0' && isspace(*pChar))
	    pChar++;
	if (*pChar == '#' || *pChar == '\0')
	    continue;
	
	// Trim trailing white space
	pChar2 = &buffer[strlen(buffer)-1];
	while (isspace(*pChar2) && (pChar >= pChar))
	{
	    *pChar2 = '\0';
	    pChar2--;
	}
	
	// If there is anything left on the line, consider it a host name
	if (strlen(pChar) > 0)
	{
	    // Copy the host name
	    pChar2 = pszHost;
	    while (*pChar != '\0' && !isspace(*pChar))
	    {
		*pChar2 = *pChar;
		pChar++;
		pChar2++;
	    }
	    *pChar2 = '\0';

	    return true;
	}
    }
    return false;
}

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

// parseCommandLine ///////////////////////////////////////////////////////////
//
// check for command line parameters and set various flags 
//
void parseCommandLine (int *argc, char** argv[])
{
    /*
    // A little snipet of code to test the update feature.
    // Uncomment this code, compile, run mpd -update, then run mpd -loser to see if this new functionality exists
    if (GetOpt(*argc, *argv, "-loser"))
    {
	printf("you are a winner\n");
	ExitProcess(0);
    }
    //*/
    if (GetOpt(*argc, *argv, "-norestart"))
	bSetupRestart = false;
    if (GetOpt(*argc, *argv, "-interact"))
    {
	interact = true;
    }
    if (GetOpt(*argc, *argv, "-remove") || GetOpt(*argc, *argv, "-unregserver") || GetOpt(*argc, *argv, "-uninstall"))
    {
	RegDeleteKey(HKEY_CURRENT_USER, MPICHKEY);
	CmdRemoveService(TRUE);
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-install") || GetOpt(*argc, *argv, "-regserver"))
    {
	bool bMPDUserCapable = false;
	char account[100]="", password[100]="", phrase[MPD_PASSPHRASE_MAX_LENGTH+1]="", port[10]="";
	char version[100]="";

	if (CmdRemoveService(FALSE) == FALSE)
	{
	    printf("Unable to remove the previous installation, install failed.\n");
	    ExitProcess(0);
	}
	
	easy_socket_init();
	CreateMPDRegistry();
	bMPDUserCapable = GetOpt(*argc, *argv, "-mpduser");
	if (GetOpt(*argc, *argv, "-phrase", phrase))
	    WriteMPDRegistry("phrase", phrase);
	if (GetOpt(*argc, *argv, "-getphrase"))
	{
	    GetPassword("passphrase for mpd: ", NULL, phrase);
	    WriteMPDRegistry("phrase", phrase);
	}
	if (GetOpt(*argc, *argv, "-port", port))
	    WriteMPDRegistry("port", port);
	if (GetOpt(*argc, *argv, "-account", account))
	{
	    if (!GetOpt(*argc, *argv, "-password", password))
		GetPassword(NULL, account, password);
	    WriteMPDRegistry("SingleUser", "yes");
	    ParseRegistry(true);
	    CmdInstallService(account, password, bMPDUserCapable);
	}
	else
	{
	    WriteMPDRegistry("SingleUser", "no");
	    ParseRegistry(true);
	    CmdInstallService(NULL, NULL, bMPDUserCapable);
	}
	GetMPDVersion(version, 100);
	WriteMPDRegistry("version", version);
	easy_socket_finalize();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-update"))
    {
	char account[100]="", password[100]="", phrase[MPD_PASSPHRASE_MAX_LENGTH+1]="", port[10]="";
	int nPort;
	char pszHost[MAX_HOST_LENGTH], pszHostFile[MAX_PATH];
	char pszFileName[MAX_PATH];
	char pszError[256];

	if (!GetOpt(*argc, *argv, "-mpd", pszFileName))
	{
	    HMODULE hModule = GetModuleHandle(NULL);
	    if (!GetModuleFileName(hModule, pszFileName, MAX_PATH))
	    {
		printf("Please specify the location of the new mpd.exe with the -mpd option, (-mpd c:\\some\\path\\mpd.exe)\n");
		ExitProcess(0);
	    }
	    printf("updating mpd to '%s'\n", pszFileName);
	}
	if (!GetOpt(*argc, *argv, "-singleuser"))
	{
	    if (GetOpt(*argc, *argv, "-account", account))
	    {
		if (!GetOpt(*argc, *argv, "-password", password))
		    GetPassword(NULL, account, password);
	    }
	    else
	    {
		printf("Enter a user to connect to the remote machines as.\naccount: ");fflush(stdout);
		gets(account);
		GetPassword(NULL, account, password);
	    }
	}

	easy_socket_init();
	nPort = MPD_DEFAULT_PORT;
	if (!ReadMPDRegistry("phrase", phrase, false))
	    strcpy(phrase, MPD_DEFAULT_PASSPHRASE);
	GetOpt(*argc, *argv, "-phrase", phrase);
	if (GetOpt(*argc, *argv, "-getphrase"))
	{
	    GetPassword("passphrase for mpd: ", NULL, phrase);
	}
	if (GetOpt(*argc, *argv, "-port", port))
	    nPort = atoi(port);
	if (GetOpt(*argc, *argv, "-hostfile", pszHostFile))
	{
	    FILE *fin = fopen(pszHostFile, "r");
	    if (fin == NULL)
	    {
		char pszStr[1024];
		Translate_Error(GetLastError(), pszStr);
		printf("Unable to open the host file '%s', %s\n", pszHostFile, pszStr);
		easy_socket_finalize();
		ExitProcess(0);
	    }

	    while (GetNextHost(fin, pszHost))
	    {
		pszError[0] = '\0';
		if (!UpdateMPD(pszHost, account, password, nPort, phrase, pszFileName, pszError, 256))
		{
		    printf("Failed to update mpd on %s:\n%s\n", pszHost, pszError);
		}
	    }
	    fclose(fin);
	}
	else
	{
	    if (!GetOpt(*argc, *argv, "-host", pszHost))
	    {
		printf("Enter the hostname where the mpd that you wish to update is running.\nhost: ");fflush(stdout);
		gets(pszHost);
	    }
	    pszError[0] = '\0';
	    if (!UpdateMPD(pszHost, account, password, nPort, phrase, pszFileName, pszError, 256))
	    {
		printf("Failed to update mpd on %s:\n%s\n", pszHost, pszError);
	    }
	}

	easy_socket_finalize();
	printf("Finished.\n");
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-iupdate"))
    {
	// This option is used internally by the update feature
	char pszOldFileName[MAX_PATH], pszNewFileName[MAX_PATH];
	char pszPid[10];
	if (GetOpt(*argc, *argv, "-pid", pszPid) && 
	    GetOpt(*argc, *argv, "-old", pszOldFileName) && 
	    GetOpt(*argc, *argv, "-new", pszNewFileName))
	{
	    UpdateMPD(pszOldFileName, pszNewFileName, atoi(pszPid));
	}
	ExitProcess(0);
    }
    char host[100];
    if (GetOpt(*argc, *argv, "-console", host))
    {
	char phrase[MPD_PASSPHRASE_MAX_LENGTH+1];
	int port = -1;
	GetOpt(*argc, *argv, "-port", &port);
	/* DoConsole destroys phrase after using it */
	DoConsole(
	    host, port, 
	    GetOpt(*argc, *argv, "-getphrase"),
	    GetOpt(*argc, *argv, "-phrase", phrase) ? phrase : NULL);
	easy_socket_finalize();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-console"))
    {
	char phrase[MPD_PASSPHRASE_MAX_LENGTH+1];
	int port = -1;
	GetOpt(*argc, *argv, "-port", &port);
	/* DoConsole destroys phrase after using it */
	DoConsole(
	    NULL, port, 
	    GetOpt(*argc, *argv, "-getphrase"),
	    GetOpt(*argc, *argv, "-phrase", phrase) ? phrase : NULL);
	easy_socket_finalize();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-start"))
    {
	CmdStartService();
	ExitProcess(0);
    }
    char pszFileName[MAX_PATH];
    if (GetOpt(*argc, *argv, "-startdelete", pszFileName))
    {
	// This option is used by the update feature to start the new service and delete the old one.
	char version[100];
	// update the version
	GetMPDVersion(version, 100);
	WriteMPDRegistry("version", version);
	// start the new mpd
	CmdStartService();
	// Give the temporary mpd time to exit
	Sleep(1000);
	// Then delete it.
	DeleteFile(pszFileName);
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-stop"))
    {
	CmdStopService();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-restart", host))
    {
	ConnectAndRestart(argc, argv, host);
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-restart"))
    {
	CmdStopService();
	Sleep(1000);
	CmdStartService();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-clean"))
    {
	CleanMPDRegistry();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-d"))
    {
	char account[100]="", password[100]="", phrase[MPD_PASSPHRASE_MAX_LENGTH+1]="", pszPort[20]="";
	char str_temp[10];
	easy_socket_init();
	CreateMPDRegistry();
	if (GetOpt(*argc, *argv, "-phrase", phrase))
	    WriteMPDRegistry("phrase", phrase);
	if (GetOpt(*argc, *argv, "-getphrase"))
	{
	    GetPassword("passphrase for mpd: ", NULL, phrase);
	    WriteMPDRegistry("phrase", phrase);
	}
	if (GetOpt(*argc, *argv, "-port", pszPort))
	{
	    int g_nSavedPort = g_nPort;
	    g_nPort = atoi(pszPort);
	    if (g_nPort > 0)
	    {
		sprintf(pszPort, "%d", g_nPort);
		WriteMPDRegistry("port", pszPort);
		//printf("using port %d\n", g_nPort);
	    }
	    else
		g_nPort = g_nSavedPort;
	}
	g_bSingleUser = true;
	g_bStartAlone = GetOpt(*argc, *argv, "-startalone");
	if (ReadMPDRegistry("SingleUser", str_temp, false))
	{
	    if (stricmp(str_temp, "no") == 0)
	    {
		WriteMPDRegistry("RevertToMultiUser", "yes");
	    }
	}
	WriteMPDRegistry("SingleUser", "yes");

	ParseRegistry(true);
	CmdDebugService(*argc, *argv);
	easy_socket_finalize();
	ExitProcess(0);
    }
    if (GetOpt(*argc, *argv, "-v") || GetOpt(*argc, *argv, "-version"))
    {
/*#define USE_BAD_NONREDIRECTABLE_VERSION*/
#ifdef USE_BAD_NONREDIRECTABLE_VERSION
	/* stdin, stdout, stderr don't get redirected for some reason when I launch this application
	   with pipes for redirecting output.  If the HANDLE version of stderr is used like the code 
	   after the else, then everything works fine. */
	char str[100];
	GetMPDVersion(str, 100);
	fprintf(stderr, "\nMPD - mpich daemon for Microsoft Windows, version %s\n%s\n", str, COPYRIGHT);
	fflush(stderr);
	ExitProcess(0);
#else
	HANDLE hErr = GetStdHandle(STD_ERROR_HANDLE);
	char str[100], out[200];
	DWORD num_written;

	GetMPDVersion(str, 100);
	sprintf(out, "\nMPD - mpich daemon for Microsoft Windows, version %s\n%s\n", str, COPYRIGHT);
	WriteFile(hErr, out, strlen(out), &num_written, NULL);
	ExitProcess(0);
#endif
    }
    if (GetOpt(*argc, *argv, "-h") || GetOpt(*argc, *argv, "-?") || GetOpt(*argc, *argv, "-help"))
    {
	char str[100];
	GetMPDVersion(str, 100);
	fprintf(stderr, "\nMPD - mpich daemon for Windows NT, version %s\n%s\n", str, COPYRIGHT);
	fprintf(stderr, "Usage:\n  mpd [ -v -h -install -remove -console ]\n\nCommand line options:\n");
	fprintf(stderr, "  -install \t:install the service\n  -install -interact    :allows the mpd to interact with the desktop\n");
	fprintf(stderr, "  -install -mpduser\t:install the service with single user commands enabled.\n");
	fprintf(stderr, "  -remove\t:remove the service\n");
	fprintf(stderr, "  -v\t\t:display version\n");
	fprintf(stderr, "  -h\t\t:this help screen\n");
	fprintf(stderr, "  -console\t:start a console session with the mpd on the current host\n");
	fprintf(stderr, "  -console host [-port x] :start a console session with the mpd on 'host:port'\n");
	fprintf(stderr, "  -d\t\t:run the mpd from the console\n");
	ExitProcess(0);
    }
}
