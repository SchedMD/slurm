#include "GetStringOpt.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

bool GetStringOpt(char *pszStr, char *pszName, char *pszValue, char *pszDelim /*= "="*/)
{
    char *pszFirst, *pszDelimLoc, *pszLast;
    bool bFirst = true;

    if (pszStr == NULL || pszName == NULL || pszValue == NULL)
	return false;

    while (true)
    {
	// Find the name
	pszFirst = strstr(pszStr, pszName);
	if (pszFirst == NULL)
	    return false;

	// Check to see if we have matched a sub-string
	if (bFirst)
	{
	    bFirst = false;
	    if ((pszFirst != pszStr) && (!isspace(*(pszFirst-1))))
	    {
		pszStr = pszFirst + strlen(pszName);
		continue;
	    }
	}
	else
	{
	    if (!isspace(*(pszFirst-1)))
	    {
		pszStr = pszFirst + strlen(pszName);
		continue;
	    }
	}

	// Skip over any white space after the name
	pszDelimLoc = &pszFirst[strlen(pszName)];
	while (isspace(*pszDelimLoc))
	    pszDelimLoc++;

	// Find the deliminator
	if (strnicmp(pszDelimLoc, pszDelim, strlen(pszDelim)) != 0)
	{
	    //pszStr = &pszDelimLoc[strlen(pszDelim)];
	    pszStr = pszDelimLoc;
	    continue;
	}
	
	// Skip over the deliminator and any white space
	pszFirst = &pszDelimLoc[strlen(pszDelim)];
	while (isspace(*pszFirst))
	    pszFirst++;

	if (*pszFirst == '\'')
	{
	    pszFirst++;
	    while (*pszFirst != '\'' && *pszFirst != '\0')
	    {
		*pszValue++ = *pszFirst++;
	    }
	    *pszValue = '\0';
	    break;
	}
	else
	{
	    // Find the next deliminator
	    pszLast = strstr(pszFirst, pszDelim);
	    if (pszLast == NULL)
	    {
		strcpy(pszValue, pszFirst);
		break;
	    }
	    
	    // Back up over any white space and name preceding the second deliminator
	    pszLast--;
	    while (pszLast > pszFirst && isspace(*pszLast))
		pszLast--;
	    while (pszLast > pszFirst && !isspace(*pszLast))
		pszLast--;
	    while (pszLast > pszFirst && isspace(*pszLast))
		pszLast--;
	    
	    // Copy the data between first and last
	    pszLast++;
	    strncpy(pszValue, pszFirst, pszLast-pszFirst);
	    pszValue[pszLast-pszFirst] = '\0';
	}
	break;
    }
    return true;
}
