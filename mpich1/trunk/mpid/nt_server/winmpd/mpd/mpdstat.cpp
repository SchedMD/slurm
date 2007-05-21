#include "mpdimpl.h"

void statMPD(char *pszParam, char *pszStr, int length)
{
    if (length < 1)
	return;
    *pszStr = '\0';
    try {
    if (stricmp(pszParam, "ps") == 0)
    {
	statProcessList(pszStr, length);
    }
    else if (stricmp(pszParam, "launch") == 0)
    {
	statLaunchList(pszStr, length);
    }
    else if (stricmp(pszParam, "config") == 0)
    {
	statConfig(pszStr, length);
    }
    else if (stricmp(pszParam, "context") == 0)
    {
	statContext(pszStr, length);
    }
    else if (stricmp(pszParam, "tmp") == 0)
    {
	statTmp(pszStr, length);
    }
    else if (stricmp(pszParam, "barrier") == 0)
    {
	statBarrier(pszStr, length);
    }
    else if (stricmp(pszParam, "forwarders") == 0)
    {
	statForwarders(pszStr, length);
    }
    else if (stricmp(pszParam, "cached") == 0)
    {
	statCachedUsers(pszStr, length);
    }
    else if ((stricmp(pszParam, "help") == 0) || (stricmp(pszParam, "?") == 0))
    {
	_snprintf(pszStr, length, 
	    " ps ......... running processes\n"
	    " launch ..... launch structures\n"
	    " config ..... mpd registry settings\n"
	    " context .... open contexts\n"
	    " tmp ........ temporary files\n"
	    " barrier .... outstanding barriers\n"
	    " forwarders . forwarders on this node\n"
	    " cached ..... cached users\n"
	    );
    }
    else
    {
	strcpy(pszStr, "<unsupported>\n");
	return;
    }
    if (strlen(pszStr) == 0)
    {
	strcpy(pszStr, "<none>\n");
    }
    } catch (...)
    {
	err_printf("exception caught in stat command.\n");
	strcpy(pszStr, "internal error");
    }
}
