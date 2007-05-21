#ifndef GET_HOSTS_H
#define GET_HOSTS_H

#define HOSTNAMELEN		100

struct HostNode
{
	char pszHost[HOSTNAMELEN];
	int nSpawns;
	bool bPrimaryMPD;
	HostNode *pNext;
};

HostNode * GetHostsFromRegistry(int nMPDsToLaunch);
HostNode * GetHostsFromFile(int nMPDsToLaunch, char *pszHostFile);
HostNode * GetHostsFromCmdLine(int argc, char **argv);

#endif
