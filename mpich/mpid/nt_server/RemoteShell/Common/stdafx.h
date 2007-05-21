/**********************************************************************
/* THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
/* ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
/* THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
/* PARTICULAR PURPOSE.
/*
/* Copyright (C) 1998  Brigham Young University.  All Rights Reserved.
/*
/* file: stdafx.h
/* note: This file exists to ensure winsock2.h gets included before afxsock.h
/*       in MFC applications.
/**********************************************************************/

#define VC_EXTRALEAN		// Exclude rarely-used stuff from Windows headers

#include <afxwin.h>         // MFC core and standard components
#include <afxext.h>         // MFC extensions
#ifndef _AFX_NO_AFXCMN_SUPPORT
#include <afxcmn.h>			// MFC support for Windows 95 Common Controls
#endif // _AFX_NO_AFXCMN_SUPPORT

#include <winsock2.h>
#include <afxsock.h>		// MFC socket extensions
