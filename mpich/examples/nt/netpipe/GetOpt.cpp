#include "GetOpt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

bool GetOpt(int &argc, char **&argv, char * flag)
{
	if (flag == NULL)
		return false;

	for (int i=0; i<argc; i++)
	{
		if (stricmp(argv[i], flag) == 0)
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

bool GetOpt(int &argc, char **&argv, char * flag, int *n)
{
	if (flag == NULL)
		return false;

	for (int i=0; i<argc; i++)
	{
		if (stricmp(argv[i], flag) == 0)
		{
			if (i+1 == argc)
				return false;
			*n = atoi(argv[i+1]);
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

bool GetOpt(int &argc, char **&argv, char * flag, long *n)
{
	int i;
	if (GetOpt(argc, argv, flag, &i))
	{
		*n = (long)i;
		return true;
	}
	return false;
}

bool GetOpt(int &argc, char **&argv, char * flag, double *d)
{
	if (flag == NULL)
		return false;

	for (int i=0; i<argc; i++)
	{
		if (stricmp(argv[i], flag) == 0)
		{
			if (i+1 == argc)
				return false;
			*d = atof(argv[i+1]);
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

bool GetOpt(int &argc, char **&argv, char * flag, char * str)
{
	if (flag == NULL)
		return false;

	for (int i=0; i<argc; i++)
	{
		if (stricmp(argv[i], flag) == 0)
		{
			if (i+1 == argc)
				return false;
			strcpy(str, argv[i+1]);
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
