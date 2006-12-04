#ifndef _RACRPT
#define _RACRPT
/*
 *	$RCSfile: racrpt.h,v $Revision: 1.19 $Date: 2006/01/10 01:17:23 $
 *****************************************************************************
 *  Copyright (C) 1993-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by: Robert Wood, bwood@llnl.gov
 *  UCRL-CODE-155981.
 *
 *  This file is part of the LCRM (Livermore Computing Resource Management)
 *  system. For details see http://www.llnl.gov/lcrm.
 *
 *  LCRM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  LCRM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with LCRM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *****************************************************************************
 *
 *	Author:	Robert R. Wood
 *
 *	Format and content definitions for the racmgr written acctlog file.
 */
#define ACCTLOG_VERSION		6
#define ACCTLOG_FORM		"\
%d %ld %s %s %s %s %d %s %s %s %d %.3lf %s %s %ld %ld %ld %ld %s %s %.3lf %d %s %s %.3lf\n"

#define ACCTLOG_HEAD		"\
%d timestamp host partition user sid jobid type pool bank nice weight ucpu icpu maxpsize maxrpsize jobpsize jobrpsize memint vmemint arus ncpus sstate class charge\n"

/*
 *	ACCTLOG_FILE is the file to which racmgr writes logs of resource usage.
 *	ACCTLOG_HEADER_FILE is the file that contains headers for data in the
 *	ACCTLOG_FILE. Both files exist in the LCRM adm directory.
 */
#if !LRM_TEST
#define ACCTLOG_FILE		"acctlog"
#define ACCTLOG_HEADER_FILE	"acctlog.hdr"
#else
#define ACCTLOG_FILE		"tacctlog"
#define ACCTLOG_HEADER_FILE	"tacctlog.hdr"
#endif

/*
 *	Define the constant variables used by acctper and acctagain.
 */
#define NO_VERSION_PARSE	0
#define PARSE_VERSION		1

/*
 *	Define the output mapping of data, in order, that is placed
 *	into the files sent to ADBHOST.
 */
#define ADB_KEYS "host partition pool timestamp user bank type ucpu icpu memint vmemint"

typedef struct {
	int		kc_cnt;
	char		*version;
	char		**kc_token;
} key_chain_t;

typedef struct {
	int		kr_cnt;
	key_chain_t	*kr_chain;
} key_ring_t;

#endif	/* _RACRPT */
