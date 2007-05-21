/**********************************************************************
/* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
/* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
/* THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
/* PARTICULAR PURPOSE.
/*
/* Copyright (C) 1998  Brigham Young University.  All Rights Reserved.
/*
/* file: Translate_Error.cpp
/*
/**********************************************************************/

// Error message code
#include "stdafx.h"
#include "Translate_Error.h"
#include <stdio.h>

// Function name	: Translate_Error
// Description	    : 
// Return type		: void 
// Argument         : int error
// Argument         : char *msg
void Translate_Error(int error, char *msg, char *prepend)
{
	HLOCAL str;
	int num_bytes;
	num_bytes = FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		error,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(LPTSTR) &str,
		0,0);
	if (prepend == NULL)
		memcpy(msg, str, num_bytes+1);
	else
		sprintf(msg, "%s%s", prepend, str);
	LocalFree(str);
}

// Function name	: Translate_HRError
// Description	    : 
// Return type		: void 
// Argument         : HRESULT hr
// Argument         : char *error_msg
void Translate_HRError(HRESULT hr, char *error_msg, char *prepend)
{
	HLOCAL str;
	FormatMessage(
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		hr,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(LPTSTR) &str,
		0,0);
	
	if (prepend == NULL)
		sprintf(error_msg, "%s", str);
	else
		sprintf(error_msg, "%s%s", prepend, str);

	LocalFree(str);
}

void Translate_Error(int error, WCHAR *msg, WCHAR *prepend)
{
	HLOCAL str;
	int num_bytes;
	num_bytes = FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		error,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(WCHAR*)&str,
		0,0);
	if (prepend == NULL)
		swprintf(msg, L"%s", str);
	else
		swprintf(msg, L"%s%s", prepend, str);
	LocalFree(str);
}

// Function name	: Translate_HRError
// Description	    : 
// Return type		: void 
// Argument         : HRESULT hr
// Argument         : char *error_msg
void Translate_HRError(HRESULT hr, WCHAR *error_msg, WCHAR *prepend)
{
	HLOCAL str;
	FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM | 
		FORMAT_MESSAGE_ALLOCATE_BUFFER,
		0,
		hr,
		MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ),
		(WCHAR*) &str,
		0,0);

	if (prepend == NULL)
		swprintf(error_msg, L"%s", str);
	else
		swprintf(error_msg, L"%s%s", prepend, str);

	LocalFree(str);
}

