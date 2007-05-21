#include "mpdutil.h"
#include <stdlib.h>
#include <malloc.h>

/*
 * This is not encryption.
 * This is simply encoding characters into number strings to
 * avoid string delimination problems.
 */

char * EncodePassword(char *pwd)
{
    int length;
    int i;
    int len;
    char *pStr, *pRetVal;

    if (pwd == NULL)
	return NULL;

    len = strlen(pwd);
    length = len * 3;
    for (i=0; i<len; i++)
    {
	if (pwd[i] > 99)
	    length++;
    }
    length++; /* add one character for the NULL termination */

    pRetVal = pStr = (char*)malloc(length);
    if (pStr == NULL)
	return NULL;
    for (i=0; i<len; i++)
    {
	sprintf(pStr, ".%d", (int)pwd[i]);
	pStr = &pStr[strlen(pStr)];
    }
    if (len == 0)
	*pRetVal = '\0';

    return pRetVal;
}

void DecodePassword(char *pwd)
{
    char *pChar, *pStr;

    if (pwd == NULL)
	return;

    pChar = pStr = pwd;

    while (*pStr != '\0')
    {
	if (*pStr == '.')
	    pStr++;
	*pChar = (char)atoi(pStr);
	pChar++;
	while ((*pStr != '.') && (*pStr != '\0'))
	    pStr++;
    }
    *pChar = '\0';
}
