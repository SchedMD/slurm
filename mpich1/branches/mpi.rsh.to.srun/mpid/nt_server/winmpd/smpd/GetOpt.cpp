#include "GetOpt.h"
#include <tchar.h>
#include <stdio.h>

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag)
{
    TCHAR flag2[100];
    bool bCheck2 = false;
    bool bFound;

    if (flag == NULL)
	return false;
    
    if (flag[0] == _T('-'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('/');
	bCheck2 = true;
    }
    if (flag[0] == _T('/'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('-');
	bCheck2 = true;
    }

    for (int i=0; i<argc; i++)
    {
	bFound = false;
	if (bCheck2 && (_tcsicmp(argv[i], flag2) == 0))
	    bFound = true;
	if (_tcsicmp(argv[i], flag) == 0)
	    bFound = true;
	if (bFound)
	{
	    for (int j=i; j<argc; j++)
	    {
		argv[j] = argv[j+1];
	    }
	    argc -= 1;
	    return true;
	}
    }
    return false;
}

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
// Argument         : int *n
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, int *n)
{
    TCHAR flag2[100];
    bool bCheck2 = false;
    bool bFound;

    if (flag == NULL)
	return false;
    
    if (flag[0] == _T('-'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('/');
	bCheck2 = true;
    }
    if (flag[0] == _T('/'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('-');
	bCheck2 = true;
    }
    
    for (int i=0; i<argc; i++)
    {
	bFound = false;
	if (bCheck2 && (_tcsicmp(argv[i], flag2) == 0))
	    bFound = true;
	if (_tcsicmp(argv[i], flag) == 0)
	    bFound = true;
	if (bFound)
	{
	    if (i+1 == argc)
		return false;
	    *n = _ttoi(argv[i+1]);
	    for (int j=i; j<argc-1; j++)
	    {
		argv[j] = argv[j+2];
	    }
	    argc -= 2;
	    return true;
	}
    }
    return false;
}

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
// Argument         : long *n
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, long *n)
{
    int i;
    if (GetOpt(argc, argv, flag, &i))
    {
	*n = (long)i;
	return true;
    }
    return false;
}

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
// Argument         : unsigned long *u
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, unsigned long *u)
{
    TCHAR flag2[100];
    bool bCheck2 = false;
    bool bFound;

    if (flag == NULL)
	return false;
    
    if (flag[0] == _T('-'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('/');
	bCheck2 = true;
    }
    if (flag[0] == _T('/'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('-');
	bCheck2 = true;
    }
    
    for (int i=0; i<argc; i++)
    {
	bFound = false;
	if (bCheck2 && (_tcsicmp(argv[i], flag2) == 0))
	    bFound = true;
	if (_tcsicmp(argv[i], flag) == 0)
	    bFound = true;
	if (bFound)
	{
	    if (i+1 == argc)
		return false;
	    *u = (unsigned long)_ttol(argv[i+1]);
	    for (int j=i; j<argc-1; j++)
	    {
		argv[j] = argv[j+2];
	    }
	    argc -= 2;
	    return true;
	}
    }
    return false;
}

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
// Argument         : double *d
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, double *d)
{
    TCHAR flag2[100];
    bool bCheck2 = false;
    bool bFound;

    if (flag == NULL)
	return false;
    
    if (flag[0] == _T('-'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('/');
	bCheck2 = true;
    }
    if (flag[0] == _T('/'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('-');
	bCheck2 = true;
    }
    
    for (int i=0; i<argc; i++)
    {
	bFound = false;
	if (bCheck2 && (_tcsicmp(argv[i], flag2) == 0))
	    bFound = true;
	if (_tcsicmp(argv[i], flag) == 0)
	    bFound = true;
	if (bFound)
	{
	    if (i+1 == argc)
		return false;
	    *d = _tcstod(argv[i+1], NULL);
	    for (int j=i; j<argc-1; j++)
	    {
		argv[j] = argv[j+2];
	    }
	    argc -= 2;
	    return true;
	}
    }
    return false;
}

// Function name	: GetOpt
// Description	    : 
// Return type		: bool 
// Argument         : int &argc
// Argument         : LPTSTR *&argv
// Argument         : LPTSTR flag
// Argument         : LPTSTR str
bool GetOpt(int &argc, LPTSTR *&argv, LPTSTR flag, LPTSTR str)
{
    TCHAR flag2[100];
    bool bCheck2 = false;
    bool bFound;

    if (flag == NULL)
	return false;
    
    if (flag[0] == _T('-'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('/');
	bCheck2 = true;
    }
    if (flag[0] == _T('/'))
    {
	_tcscpy(flag2, flag);
	flag2[0] = _T('-');
	bCheck2 = true;
    }
    
    for (int i=0; i<argc; i++)
    {
	bFound = false;
	if (bCheck2 && (_tcsicmp(argv[i], flag2) == 0))
	    bFound = true;
	if (_tcsicmp(argv[i], flag) == 0)
	    bFound = true;
	if (bFound)
	{
	    if (i+1 == argc)
		return false;
	    if (argv[i+1][0] == _T('-') || argv[i+1][0] == _T('/')) // If the next argument is another option flag, it's a no-match
		return false;
	    _tcscpy(str, argv[i+1]);
	    for (int j=i; j<argc-1; j++)
	    {
		argv[j] = argv[j+2];
	    }
	    argc -= 2;
	    return true;
	}
    }
    return false;
}
