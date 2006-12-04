/*
 *	$RCSfile: moab_acctper.c,v $Revision: 0.1 $Date: 2006/12/04 01:23:07 $
 *****************************************************************************
 *  Copyright (C) 1993-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by: Danny Auble, da@llnl.gov
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
 *	Author:	Danny Auble
 *
 *	moab_acctper moves moab accounting records to lcrm archive files. It is invoked by the
 *	racmgr just after the racmgr has moved its ACCTLOG_FILE to
 *	ACCTLOG_FILE.temp. It copies the records found in ACCTLOG_FILE.temp to
 *	the current accounting file in the lrm adm ACCT_ARCHIVE_DIR directory.
 *	The current accounting file is named "acctlog.Y.M.d.h" where the
 *	h is equal to the hour of the current execution time of acctper. In
 *	addition to appending the records of ACCTLOG_FILE.temp to the
 *	current accounting file, acctper copies selected (via ADB_KEYS) fields
 *	from the records to a file that is sent to the ADBHOST found in the
 *	CFG_FILE.
 */
#include <libgen.h>
#include <dirent.h>

#include "lrm_install.h"
#include "lrm_log.h"
#include "liblrm_int.h"
#include "liblrmsup.h"
#include "racrpt.h"
#include "lrm_config.h"

/*
 *	Define the constant variables used in this module.
 */
#define ACCT_ADBSEND_DIR	"archive/acct/tosend"
#define ACCT_ARCHIVE_DIR	"archive/acct"

static const char *COMPRESS	= "/usr/bin/compress";
static const char *RM		= "/usr/bin/rm";
static const char *TAR		= "/usr/bin/tar -cf";

static const int COMPRESS_PERIOD  = 60 * 60 * 24 * 1;	/*  1 day  */
static const int TAR_PERIOD       = 60 * 60 * 24 * 3;	/*  3 days */
static const int TAR_PURGE_PERIOD = 60 * 60 * 24 * 10;	/* 10 days */

/*
 *	Declare external variables.
 */

/*
 *	Declare global static variables.
 */
static char		adbdir[LRM_MAXPATHLEN];
static char		adbhost[MAXHOSTNAMELEN];
static char		adbuser[LRM_MAXNAMELEN];
static char		inputfile[LRM_MAXPATHLEN];
static char		logmsg[1024];
static char		conhost[MAXHOSTNAMELEN];
static char		scp_cmd[LRM_MAXPATHLEN];

static FILE *_open_log_file(char *filename);

/* _open_log_file() -- find the current or specified log file, and open it
 *
 * IN:		file name
 * RETURNS:	open FILE *
 *
 * Side effects:
 * 	- Sets opt_filein to the current system accounting log unless
 * 	  the user specified another file.
 */

static FILE *_open_log_file(char *filename)
{
	FILE *fd = fopen(filename, "r");
	if (fd == NULL) {
		perror(filename);
		exit(1);
	}
	return fd;
}

/*
 *	check_residency checks that acctper is executing on the control
 *	host. It exits if there is a problem.
 */
static void check_residency(void)
{
	extern char	gateway[];
	extern char	*lrm_proc_name;
	int		lrmstat;

	if (!getconhost(conhost, sizeof(conhost), &lrmstat)) {
		fprintf(stderr, "getconhost() error %d.\n", lrmstat);
		exit(1);
	}
	if (!streq(gateway, conhost)) {
		fprintf(stderr, "%s must run on control host only.\n",
			lrm_proc_name);
		exit(1);
	}

	return;
}

/*
 *	get_acctlog_hdr_contents reads the acctlog.hdr file into memory and
 *	returns a pointer to the memory read. On error, this routine exits.
 */
static char *get_acctlog_hdr_contents(void)
{
	lbf_desc_t	lbf;

	memset(&lbf, 0, sizeof(lbf));
	lrm_admpath(ACCTLOG_HEADER_FILE, lbf.lbf_file_name,
		    sizeof(lbf.lbf_file_name));
	if (lrm_buffer_file(&lbf) == LRM_BUFFER_FAILED) {
		sprintf(logmsg, "lrm_buffer_file(%s) error %d",
			lbf.lbf_file_name, lbf.lbf_stat);
		logerr(logmsg);
		exit(1);
	}

	return(lbf.lbf_buffer);
}

/*
 *	get_key_chain takes a line of blank separated tokens. It breaks the
 *	line into an array of tokens stored in the return variable key_chain.
 *	It is the responsibility of the caller to free the allocated
 *	array of pointers to the tokens. On error, this routine logs the error
 *	and exits.
 */
static key_chain_t get_key_chain(char *line, int parse_version)
{
	key_chain_t	key_chain;

	if (parse_version) {
		key_chain.version = line;
		if ((line = strchr(line, ' ')) == NULL) {
			printf("token string error");
			exit(1);
		} else *line++ = '\0';
	}
	key_chain.kc_cnt = lrm_string2tokens(line, " \n", &key_chain.kc_token,
					     LRM_DESTRUCTIVE_LIST);

	return(key_chain);
}

/*
 *	get_acctlog_keys gets the keywords for data in the acctlog. It stores
 *	the keywords in a key_ring. Each key_ring contains a count of key_chains
 *	and an array of key_chain pointers. Each key_chain contains a version,
 *	a count of tokens and an array of key_token pointers. The key_ring is
 *	returned. This routine allocates a character buffer into which is read
 *	the contents of the acctlog.hdr file. The pointers in the key_ring
 *	point to locations in the buffer on return. So, the buffer and the
 *	key_ring contents must be freed at the same time. On error, this
 *	routine logs the error and exits.
 */
static void get_acctlog_keys(key_ring_t *key_ring)
{
	key_chain_t	*kc;
	char		*buf, **toka;
	int		i, tcnt;

	buf = get_acctlog_hdr_contents();
	tcnt = lrm_string2tokens(buf, "\n", &toka, LRM_DESTRUCTIVE_LIST);
	key_ring->kr_cnt = tcnt;
	if ((key_ring->kr_chain = calloc(tcnt, sizeof(key_chain_t))) == NULL) {
		printf("calloc() error %d", errno);
		exit(1);
	}
	for (i = 0, kc = key_ring->kr_chain; i < tcnt; i++, kc++)
		*kc = get_key_chain(toka[i], PARSE_VERSION);
	free(toka);

	return;
}

/*
 *	get_input_file opens the ACCTLOG_FILE.temp file and returns its FILE
 *	pointer to the caller. It exits on error.
 */
static FILE *get_input_file(void)
{
	FILE	*fp;

	(void) lrm_admpath(ACCTLOG_FILE, inputfile, sizeof(inputfile));

	if ((strlen(inputfile) + 6) > sizeof(inputfile)) {
		sprintf(logmsg, "resource usage log file name %s is too long",
			inputfile);
		logerr(logmsg);
		exit(1);
	}
	strcat(inputfile, ".temp");

	if ((fp = fopen(inputfile, "r")) == NULL) {
		sprintf(logmsg, "fopen(%s) error %d", inputfile, errno);
		logerr(logmsg);
		exit(1);
	}

	return(fp);
}

/*
 *	get_archive_file generates the archived file path name as of the
 *	current hour. If this file does not exist, it is created. In either
 *	case it is opened with append access. The routine process_data will
 *	read data from the ACCTLOG_FILE.temp file and write it to the archived
 *	file. In this way, the acctlog file can be terminated often while
 *	a full hour's of records can go to an archived file.
 */
static FILE *get_archive_file(char *dir, char *timestr)
{
	char		fullpath[LRM_MAXPATHLEN];
	FILE		*fp;

	memset(fullpath, 0, sizeof(fullpath));
	sprintf(fullpath, "%s/%s.%s", dir, ACCTLOG_FILE, timestr);

	if ((fp = fopen(fullpath, "a")) == NULL) {
		sprintf(logmsg, "fopen(%s) error %d", fullpath, errno);
		logerr(logmsg);
		exit(1);
	}

	return(fp);
}

/*
 *	get_send_file creates and opens for write access a file in the
 *	ACCT_ADBSEND_DIR directory to which filtered data bound for the
 *	adbhost can be written. The name is constructed and the FILE pointer
 *	is returned.
 */
static FILE *get_send_file(char *dir, char *timestr)
{
	char		fullpath[LRM_MAXPATHLEN];
	FILE		*fp;

	memset(fullpath, 0, sizeof(fullpath));
#if LRM_TEST
	sprintf(fullpath, "%s/tlcrm_%s_%s.dat", dir, conhost, timestr);
#else
	sprintf(fullpath, "%s/lcrm_%s_%s.dat", dir, conhost, timestr);
#endif

	if ((fp = fopen(fullpath, "w")) == NULL) {
		sprintf(logmsg, "fopen(%s) error %d", fullpath, errno);
		logerr(logmsg);
		exit(1);
	}

	return(fp);
}

/*
 *	init_directories makes sure that the directories needed to hold
 *	accounting files are present and creates them if they're not. If the
 *	needed directories can't be made, we can't proceed, so exit.
 */
static void init_directories(char *archive_dir, char *send_dir)
{
	struct stat	statbuf;

	if (stat(archive_dir, &statbuf) != 0) {
		if (mkdir(archive_dir, 0750) < 0) {
			sprintf(logmsg,
				"Failed to make %s directory with error %d",
				archive_dir, errno);
			logerr(logmsg);
			exit(1);
		}
		lrm_chown(archive_dir, 0750);
	}

	if (stat(send_dir, &statbuf) != 0) {
		if (mkdir(send_dir, 0750) < 0) {
			sprintf(logmsg,
				"Failed to make %s directory with error %d",
				send_dir, errno);
			logerr(logmsg);
			exit(1);
		}
		lrm_chown(send_dir, 0750);
	}

	return;
}

/*
 *	send_files_to_adbhost sends whatever accounting records found in the
 *	send_dir directory to the adbhost server. Files that fail to be copied
 *	to the adbhost remain in the send_dir. Future invocations of
 *	send_files_to_adbhost will re-attempt to copy them.
 */
static void send_files_to_adbhost(int adb_valid, char *send_dir)
{
	struct dirent	*tdir;
	DIR		*dirp;
	char		*fname;
	char		fullpath[LRM_MAXPATHLEN];
#if !LRM_TEST
	char		cmd[256];
	int		was_sent;
#endif

	if ((dirp = opendir(send_dir)) == NULL) {
		sprintf(logmsg, "opendir(%s) errno %d", send_dir, errno);
		logerr(logmsg);
		return;
	}
	while ((tdir = readdir(dirp)) != NULL) {
		fname = tdir->d_name;
		if (*fname == '.') continue;
		sprintf(fullpath, "%s/%s", send_dir, fname);
/*
 *		In a test system, don't send test logs. Remove them and
 *		leave production logs alone. In a production system, leave
 *		test logs alone and send production logs before removing them.
 */
		if (*fname == 't') {
#if LRM_TEST
			unlink(fullpath);
#endif
			continue;
		}
#if !LRM_TEST
		chmod(fullpath, 0644);
		was_sent = 1;
		if (adb_valid) {
			sprintf(cmd, "%s %s %s@%s:%s 2>/dev/null",
				scp_cmd, fullpath, adbuser, adbhost, adbdir);
/*
 *			The cmd below routes output. Consequently, don't use the
 *			lrm_system() routine.
 */
			was_sent = system(cmd);
			was_sent = WIFEXITED(was_sent) &&
				   (WEXITSTATUS(was_sent) == 0);
		}
		if (was_sent) unlink(fullpath);
#endif
	}
	closedir(dirp);

	return;
}

/*
 *	format_dates constructs the current time in 2 string formats. The first
 *	is MM.DD.hh and the second is yy.MM.DD.hh.mm.ss. The first format
 *	is used to construct file names for files to be kept on the local
 *	machine. The second is used to construct file names for files sent to
 *	the accounting server.
 */
static void format_dates(char *time_in_hours, char *time_in_mins)
{
	time_t		Now;
	struct tm	*mt;

	time(&Now);
	mt = localtime(&Now);
	sprintf(time_in_hours, "%.4d.%.2d.%.2d.%.2d.%s",
		mt->tm_year + 1900,
		mt->tm_mon + 1,
		mt->tm_mday,
		mt->tm_hour,
		tzname[mt->tm_isdst]);
	sprintf(time_in_mins, "%.4d.%.2d.%.2d.%.2d.%.2d.%s",
		mt->tm_year + 1900,
		mt->tm_mon + 1,
		mt->tm_mday,
		mt->tm_hour,
		mt->tm_min,
		tzname[mt->tm_isdst]);

	return;
}

/*
 *	filter_acc places into a filtered line the values associated with the
 *	keywords in ADB_KEYS. The values are taken from the input variable line.
 *	The appropriate key_chain is found from the first token in the line.
 *	This token is the version. It must match key_chain->version for some
 *	value n where (key_chain == key_ring->kr_chain[n]). If the appropriate
 *	key_chain is not found, a null pointer is returned. Otherwise, a pointer
 *	to a static character array is returned. This array will have been set
 *	to the string that should be placed into the accounting file.
 */
static char *filter_acc(char *line, key_ring_t *key_ring, key_chain_t *adb_data)
{
	static char	out_string[1024];
	char		*out_tok;
	key_chain_t	line_value, *key_chain;
	int		i, j;

	memset(out_string, 0, sizeof(out_string));
	line_value = get_key_chain(line, PARSE_VERSION);

	key_chain = key_ring->kr_chain;
	for (i = 0; i < key_ring->kr_cnt; i++, key_chain++) {
		if (streq(line_value.version, key_chain->version)) break;
	}
	if (i >= key_ring->kr_cnt) return(NULL);

	for (i = 0; i < adb_data->kc_cnt; i++) {
		for (j = 0; j < key_chain->kc_cnt; j++)
			if (streq(adb_data->kc_token[i],
				  key_chain->kc_token[j]))
				break;
		if (j >= key_chain->kc_cnt) out_tok = "(NULL)";
		else out_tok = line_value.kc_token[j];
		if (i != 0) strcat(out_string, "\t");
		strcat(out_string, out_tok);
	}
	strcat(out_string, "\n");
	free(line_value.kc_token);

	return(out_string);
}

/*
 *	process_data reads lines of data from the ifp (input file), appends
 *	each line read to the afp (archive file), munges the line for output
 *	to the adb host and writes the munged data to the sfp (send file).
 */
static void process_data(FILE *ifp, FILE *afp, FILE *sfp, key_ring_t *key_ring,
			 key_chain_t *adb_data)
{
	char	line[512], *filtered_line;

	while (fgets(line, sizeof(line), ifp) != NULL) {
		if (fwrite(line, strlen(line), 1, afp) == 0) {
			logerr("Failed to write record to archive file");
			return;
		}
		if (sfp) {
			filtered_line = filter_acc(line, key_ring, adb_data);
			if (filtered_line == NULL) continue;
			if (fwrite(filtered_line, strlen(filtered_line), 1, sfp)
			    == 0) {
				logerr("Failed to write record to send file");
				return;
			}
		}
	}
/*
 *	The following line serves as a mutex with the racmgr. As long as the
 *	ACCTLOG_FILE.temp file exists, the racmgr will refrain from switching
 *	its ACCTLOG_FILE file (to ACCTLOG_FILE.temp) and it will refrain from
 *	invoking multiple instances of acctper. Once this file is unlinked, the
 *	racmgr will resume ACCTLOG_FILE archiving on the next period.
 */
	unlink(inputfile);

	return;
}

/*
 *	cleanup_accounting_files
 *	1. compresses accounting files older than the COMPRESS_PERIOD
 *	2. tars up compressed files older than the TAR_PERIOD
 *	3. removes tar files older than the TAR_PURGE_PERIOD
 */
static void cleanup_accounting_files(char *archive_dir, char *send_dir)
{
	DIR		*dirp;
	char		*fname, *suffix, cmd[256];
	char		fullpath[LRM_MAXPATHLEN];
	struct dirent	*tdir;
	struct stat	statbuf;
	struct tm	*mt;
	time_t		Now;
	int		ret;

	time(&Now);

	if ((dirp = opendir(archive_dir)) == NULL) {
		sprintf(logmsg, "opendir(%s) errno %d", archive_dir, errno);
		logerr(logmsg);
		return;
	}
	while ((tdir = readdir(dirp)) != NULL) {
		fname = tdir->d_name;
		if ((*fname == '.') || streq(basename(send_dir), fname))
			continue;
		if (!nstreq(fname, ACCTLOG_FILE,strlen(ACCTLOG_FILE))) continue;
		sprintf(fullpath, "%s/%s", archive_dir, fname);
		if (stat(fullpath, &statbuf) != 0) continue;

		suffix = strrchr(fname, '.');
/*
 *		If suffix is non-null and if the character after the period is
 *		"tar", we're looking at a tar file. If it's sufficiently old,
 *		remove it.
 */
		if ((suffix != NULL) && streq(suffix, ".tar")) {
			if ((statbuf.st_mtime + TAR_PURGE_PERIOD) <= Now) {
				unlink(fullpath);
			}
			continue;
		}
/*
 *		If suffix is non-null and if the character after the period is
 *		"Z", we're looking at a compressed file. If it's sufficiently
 *		old, tar up all compressed files from the same day.
 */
		if ((suffix != NULL) && streq(suffix, ".Z")) {
			if ((statbuf.st_mtime + TAR_PERIOD) <= Now) {
				mt = localtime(&statbuf.st_mtime);
				sprintf(fullpath, "%s/%s.%.4d.%.2d.%.2d",
					archive_dir, ACCTLOG_FILE,
					mt->tm_year + 1900,
					mt->tm_mon + 1,
					mt->tm_mday);

				sprintf(cmd, "%s %s.tar %s*.Z", TAR, fullpath,
					fullpath);
/*
 *				The cmds below rely on globbing. Consequently,
 *				don't use the lrm_system() routine.
 */
				ret = system(cmd);
				if (WIFEXITED(ret) && (WEXITSTATUS(ret) == 0)) {
					sprintf(cmd, "%s %s*.Z", RM, fullpath);
					(void) system(cmd);
				}
			}
			continue;
		}
/*
 *		Compress accounting files.
 */
		if ((statbuf.st_mtime + COMPRESS_PERIOD) <= Now) {
			sprintf(cmd, "%s %s", COMPRESS, fullpath);
			(void) lrm_system(cmd);
		}
	}
	closedir(dirp);

	return;
}

/*
 *	init_adb_interface initializes accounting database related variables.
 *	This function returns a boolean that indicates whether the appropriate
 *	configuration variables were read from the CFG_FILE.
 */
static int init_adb_interface(void)
{
	lrm_fgetenv(CFG_FILE, ADBDIR_TAG, ADBDIR, adbdir, sizeof(adbdir));
	lrm_fgetenv(CFG_FILE, ADBHOST_TAG, NULL, adbhost, sizeof(adbhost));
	lrm_fgetenv(CFG_FILE, ADBUSER_TAG, ADBUSER, adbuser, sizeof(adbuser));
	lrm_fgetenv(CFG_FILE, ADB_SCP_CMD_TAG, ADB_SCP_CMD, scp_cmd,
		    sizeof(scp_cmd));

	return((adbdir[0] != '\0') && (adbhost[0] != '\0') &&
	       (adbuser[0] != '\0') && (scp_cmd[0] != '\0'));
}

int main(int argc, char *argv[])
{
	FILE		*afp = NULL, *ifp = NULL, *sfp = NULL;
	char		adb_line[sizeof(ADB_KEYS)];
	char		archive_dir[LRM_MAXPATHLEN];
	char		send_dir[LRM_MAXPATHLEN];
	char		time_in_hours[24], time_in_mins[24];
	key_chain_t	adb_data;
	key_ring_t	key_ring;
	int		adb_valid;

	/*doesn't appear we need these functions */
	/* lrm_util_init(argv[0]); */
/* 	check_residency(); */

	/* may need this */
	adb_valid = init_adb_interface();
	get_acctlog_keys(&key_ring);
	strcpy(adb_line, ADB_KEYS);
	adb_data = get_key_chain(adb_line, NO_VERSION_PARSE);
/*
 *	Get the date formats.
 */
	format_dates(time_in_hours, time_in_mins);
/*
 *	Make sure the archive directory and directory that holds the files that
 *	get sent to the adbhost are ready for use.
 */
	lrm_admpath(ACCT_ARCHIVE_DIR, archive_dir, sizeof(archive_dir));
	lrm_admpath(ACCT_ADBSEND_DIR, send_dir, sizeof(send_dir));
	init_directories(archive_dir, send_dir);

/*
 *	Get file pointers. The input file is the ACCTLOG_FILE.temp file that
 *	the racmgr has been populating with accounting records. The archive
 *	file is the file on this host to which acctlog records are moved without
 *	alteration. This file is created once per hour and contains the
 *	records for that hour. The send file is created in the ACCT_ADBSEND_DIR
 *	directory and will contain filtered records destined for the adbhost
 *	server.
 */
	ifp = get_input_file();
	afp = get_archive_file(archive_dir, time_in_hours);
	if (adb_valid)
		sfp = get_send_file(send_dir, time_in_mins);
	process_data(ifp, afp, sfp, &key_ring, &adb_data);
	fclose(ifp);
	fclose(afp);
	if (adb_valid) fclose(sfp);
	send_files_to_adbhost(adb_valid, send_dir);
	cleanup_accounting_files(archive_dir, send_dir);

	return(0);
}
