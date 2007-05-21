#include "StringOpt.h"
#include <ctype.h>

// Function name	: GetStringOpt
// Description	    : 
// Return type		: int 
// Argument         : char *pszString
// Argument         : char cOpt
// Argument         : char *pOutString
int GetStringOpt(char *pszString, char cOpt, char *pOutString)
{
	bool bFound = false;
	char *token;

	*pOutString = '\0';

	token = pszString;
	while (isspace(*token))
		token++;
	while (*token != '\0')
	{
		if (*token == cOpt)
			bFound = true;

		token++;
		if (*token == '\'')
			token++;
		while (*token != '\'' && *token != '\0')
		{
			if (bFound)
			{
				if ((*token == '\\') && (*(token+1) == '\''))
					token++;
				*pOutString = *token;
				pOutString++;
			}
			token++;
		}
		if (bFound)
		{
			*pOutString = '\0';
			return 0;
		}
		if (*token != '\0')
			token++;
	}
	return 1;
}
