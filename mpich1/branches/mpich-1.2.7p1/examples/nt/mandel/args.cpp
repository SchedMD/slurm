#include <stdio.h>
#include <stdlib.h>
#include "args.h"
#include "lists.h"
#include <string.h>

int GetIntArg(int * argc, char **argv, char *switchName, int * val)
{
	int i, nremove;		/* nremove - number of arguments to remove */
	
	for (i = 1; i<*argc; i++) 
	{		  /* loop through all args */
		if (!strcmp(switchName, argv[i])) 
		{ /* if this is the switch we want, */
			if (i + 1<*argc) 
			{			  /* make sure there is one more */
				if (sscanf(argv[i + 1], "%d", val)) 
				{
					nremove = 2;			 /* got valid value */
				} 
				else 
				{
					nremove = 1;			 /* didn't get valid value, but */
					/* don't throw away the next arg. */
				}
			} 
			else 
			{
				nremove = 1;
			}
			for (i += nremove; i<*argc; i++) 
			{	 /* move everyone else down */
				argv[i-nremove] = argv[i];
			}
			(*argc) -= nremove;
			if (nremove == 2) return 1;		 /* if we got a value, return */
			i--;
		}
	}
	return 0;
}



int GetDoubleArg(int * argc, char ** argv, char *switchName, double *val)
{
	int i, nremove;		/* nremove - number of arguments to remove */
	
	for (i = 1; i<*argc; i++) 
	{		  /* loop through all args */
		if (!strcmp(switchName, argv[i])) 
		{ /* if this is the switch we want, */
			if (i + 1<*argc) 
			{			  /* make sure there is one more */
				if (sscanf(argv[i + 1], "%lf", val)) 
				{
					nremove = 2;			 /* got valid value */
				} 
				else 
				{
					nremove = 1;			 /* didn't get valid value, but */
					/* don't throw away the next arg. */
				}
			} 
			else 
			{
				nremove = 1;
			}
			for (i += nremove; i<*argc; i++) 
			{	 /* move everyone else down */
				argv[i-nremove] = argv[i];
			}
			(*argc) -= nremove;
			if (nremove == 2) return 1;		 /* if we got a value, return */
			i--;
		}
	}
	return 0;
}



int GetStringArg(int * argc, char **argv, char *switchName, char **val)
{
	int i, nremove;		/* nremove - number of arguments to remove */
	xpand_list_String *listStr;
	
	listStr = String_CreateList(10);
	
	for (i = 1; i<*argc; i++) 
	{		  /* loop through all args */
		if (!strcmp(switchName, argv[i])) 
		{ /* if this is the switch we want, */
			if (i + 1<*argc) 
			{			  /* make sure there is one more */
				*val = argv[i + 1];
				nremove = 2;
			} 
			else 
			{
				nremove = 1;
			}
			for (i += nremove; i<*argc; i++) 
			{	 /* move everyone else down */
				argv[i-nremove] = argv[i];
			}
			(*argc) -= nremove;
			if (nremove == 2) return 1;		 /* if we got a value, return */
			i--;
		}
	}
	return 0;
}



int IsArgPresent(int *argc, char **argv, char *switchName)
{
	int i, returnVal;
	
	returnVal = 0;
	for (i = 1; i<*argc; i++) 
	{		 /* loop through all args */
			 /*
			 printf("Comparing :%s: and :%s:\n", switchName, argv[i]);
		*/
		if (!strcmp(switchName, argv[i])) 
		{ /* if this is the switch we want, */
		  /*
		  printf("YUP!");
			*/
			for (i++; i<*argc; i++) 
			{	 /* slide everything on down */
				argv[i-1] = argv[i];
			}
			(*argc)--;
			i--;
			returnVal = 1;
		}
	}
	return returnVal;
}




int GetArgAdjacentString(int *argc, char **argv, char *switchName, char **value)
{
	int argNum, str_len;
	xpand_list_String *listStr;
	char *readPtr, *theString;
	
	listStr = String_CreateList(10);
	
	for (argNum = 1; argNum<*argc; argNum++) 
	{
		readPtr = strstr(argv[argNum], switchName);
		if (readPtr == argv[argNum]) 
		{
			/* we want to find the switch at the beginning of an argument */
			readPtr = argv[argNum] + strlen(switchName);
			while (*readPtr) 
			{
				String_AddItem(listStr, *readPtr); /* add a character */
				readPtr++;
			}	/* done copying list */
			String_AddItem(listStr, '\0');	       /* terminate the string */
			
			for (argNum++; argNum < *argc; argNum++) 
			{
				argv[argNum-1] = argv[argNum]; /* shift remaining arguments down */
			}
			(*argc)--;
			
			ListClose(listStr, theString, str_len);
			*value = theString;
			return 1;
		} /* if strstr(... switch) */
	} /* keep looking for the switch */
	return 0;			/* didn't even find the switch */
}




int GetIntListArg(int *argc, char **argv, char *switchName, int **intList, int *listLen)
{
	char *list, *token;
	xpand_list_Int *tempIntList;
	int temp_int;
	
	tempIntList = Int_CreateList(10);
	
	if (!GetArgAdjacentString(argc, argv, switchName, &list)) 
	{
		return 0;
	}
	token = strtok(list, ",");
	while (token) 
	{
		if (sscanf(token, "%d", &temp_int))
			Int_AddItem(tempIntList, temp_int);
		token = strtok((char*)0, ",");
	}
	
	ListClose(tempIntList, *intList, *listLen);
	free(list);
	return 1;
}

int GetStringListArg(int *argc, char **argv, char *switchName, char ***strList, int *listLen)
{
	char *list, *token, *str_dup;
	xpand_list_Strings *tempStrList;
	
	tempStrList = Strings_CreateList(10);
	if (!GetArgAdjacentString(argc, argv, switchName, &list)) 
	{
		return 0;
	}
	token = strtok(list, ",");
	while (token) 
	{
		str_dup = (char *) malloc((strlen(token) + 1) * sizeof(char));
		strcpy(str_dup, token);
		Strings_AddItem(tempStrList, str_dup);
		token = strtok((char*)0, ",");
	}
	ListClose(tempStrList, *strList, *listLen);
	free(list);
	return 1;
}

