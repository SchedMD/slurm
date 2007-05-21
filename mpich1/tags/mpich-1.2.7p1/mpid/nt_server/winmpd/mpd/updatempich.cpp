#include "mpdimpl.h"
#include "Translate_Error.h"
#include "Service.h"
#include "mpdutil.h"

void UpdateMPICH(char *pszFileName)
{
    int error;
    char pszStr[4096];
    char *filename = NULL, *name_part;
    DWORD len;

    len = SearchPath(NULL, "mpich.dll", NULL, 0, filename, &name_part);

    if (len == 0)
    {
	if (GetWindowsDirectory(pszStr, 4096))
	{
	    strcat(pszStr, "\\system32\\mpich.dll");
	    if (!MoveFileEx(pszFileName, pszStr, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
	    {
		error = GetLastError();
		Translate_Error(error, pszStr);
		err_printf("Unable to move '%s' to '%s'\nError: %s\n", pszFileName, filename, pszStr);
		return;
	    }
	}
	err_printf("unable to find mpich.dll\n");
	return;
    }

    filename = new char[len*2+2];
    len = SearchPath(NULL, "mpich.dll", NULL, len*2, filename, &name_part);
    if (len == 0)
    {
	delete filename;
	err_printf("unable to find mpich.dll\n");
	return;
    }

    if (!MoveFileEx(pszFileName, filename, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	err_printf("Unable to move '%s' to '%s'\nError: %s\n", pszFileName, filename, pszStr);
	delete filename;
	return;
    }

    delete filename;
}

void UpdateMPICHd(char *pszFileName)
{
    int error;
    char pszStr[4096];
    char *filename = NULL, *name_part;
    DWORD len;

    len = SearchPath(NULL, "mpichd.dll", NULL, 0, filename, &name_part);

    if (len == 0)
    {
	if (GetWindowsDirectory(pszStr, 4096))
	{
	    strcat(pszStr, "\\system32\\mpichd.dll");
	    if (!MoveFileEx(pszFileName, pszStr, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
	    {
		error = GetLastError();
		Translate_Error(error, pszStr);
		err_printf("Unable to move '%s' to '%s'\nError: %s\n", pszFileName, filename, pszStr);
		return;
	    }
	}
	err_printf("unable to find mpichd.dll\n");
	return;
    }

    filename = new char[len*2+2];
    len = SearchPath(NULL, "mpichd.dll", NULL, len*2, filename, &name_part);
    if (len == 0)
    {
	delete filename;
	err_printf("unable to find mpichd.dll\n");
	return;
    }

    if (!MoveFileEx(pszFileName, filename, MOVEFILE_COPY_ALLOWED | MOVEFILE_REPLACE_EXISTING))
    {
	error = GetLastError();
	Translate_Error(error, pszStr);
	err_printf("Unable to move '%s' to '%s'\nError: %s\n", pszFileName, filename, pszStr);
	delete filename;
	return;
    }

    delete filename;
}
