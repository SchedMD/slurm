#include "mpdutil.h"
#include <ctype.h>
#include <stdlib.h>

static void backup_whitespace(char *first, char *&p)
{
    if (p <= first)
	return;
    p--;
    while (p>=first && isspace(*p))
	p--;
    p++;
}

static bool backup_number(char *first, char *&p)
{
    if (p <= first)
	return false;
    p--;
    while (p>=first && isdigit(*p))
	p--;
    p++;
    return true;
}

static void whitespace(char *&p)
{
    if (p == NULL)
	return;
    while (*p != '\0' && isspace(*p))
	p++;
}

static int number(char *&p)
{
    int n;
    if (p == NULL)
	return -1;
    if (!isdigit(*p))
	return -1;
    n = atoi(p);
    while (*p != '\0' && isdigit(*p))
	p++;
    return n;
}

static int month_string_to_number(char *p)
{
    if (p == NULL)
	return 0;
    if (strnicmp(p, "jan", 3) == 0)
	return 1;
    if (strnicmp(p, "feb", 3) == 0)
	return 2;
    if (strnicmp(p, "mar", 3) == 0)
	return 3;
    if (strnicmp(p, "apr", 3) == 0)
	return 4;
    if (strnicmp(p, "may", 3) == 0)
	return 5;
    if (strnicmp(p, "jun", 3) == 0)
	return 6;
    if (strnicmp(p, "jul", 3) == 0)
	return 7;
    if (strnicmp(p, "aug", 3) == 0)
	return 8;
    if (strnicmp(p, "sep", 3) == 0)
	return 9;
    if (strnicmp(p, "oct", 3) == 0)
	return 10;
    if (strnicmp(p, "nov", 3) == 0)
	return 11;
    if (strnicmp(p, "dec", 3) == 0)
	return 12;
    return 0;
}

static void month_number_to_string(int n, char *p)
{
    if (p == NULL)
	return;
    switch (n)
    {
    case 1:
	strcpy(p, "Jan");
	break;
    case 2:
	strcpy(p, "Feb");
	break;
    case 3:
	strcpy(p, "Mar");
	break;
    case 4:
	strcpy(p, "Apr");
	break;
    case 5:
	strcpy(p, "May");
	break;
    case 6:
	strcpy(p, "Jun");
	break;
    case 7:
	strcpy(p, "Jul");
	break;
    case 8:
	strcpy(p, "Aug");
	break;
    case 9:
	strcpy(p, "Sep");
	break;
    case 10:
	strcpy(p, "Oct");
	break;
    case 11:
	strcpy(p, "Nov");
	break;
    case 12:
	strcpy(p, "Dec");
	break;
    default:
	strcpy(p, "mon");
	break;
    }
}

static unsigned int encode_version(int version, int major, int minor, int subminor, int year, int month, int day)
{
    return 
	((unsigned int)day & 0x1F) +
	(((unsigned int)month & 0xF) << 5) +
	(((unsigned int)year & 0x7FF) << 9) +
	(((unsigned int)minor & 0xF) << 20) +
	(((unsigned int)major & 0xF) << 24) +
	(((unsigned int)version & 0xF) << 28);
}


/*@
   mpd_version_int_to_string - convert version to string

   Parameters:
+  unsigned int n - version
-  char *str - string

   Notes:
@*/
void mpd_version_int_to_string(unsigned int n, char *str)
{
    int version=0, major=0, minor=0, month=0, day=0, year=0;
    char monthstr[4];
    if (str == NULL)
	return;
    day = (n & 0x1F);
    month = ((n >> 5) & 0xF);
    year = ((n >> 9) & 0x7FF);
    minor = ((n >> 20) & 0xF);
    major = ((n >> 24) & 0xF);
    version = ((n >> 28) & 0xF);
    month_number_to_string(month, monthstr);
    sprintf(str, "%d.%d.%d %s %d %d", version, major, minor, monthstr, day, year);
}

/*@
   mpd_version_string_to_int - convert string to version

   Parameters:
+  char *version_str - version string

   Notes:
   format = version.major.minor month day year
   eg. 1.2.3 Mar 2 2002
@*/
unsigned int mpd_version_string_to_int(char *version_str)
{
    int version=0, major=0, minor=0, subminor=0, month=0, day=0, year=0;
    char monthstr[4];
    char *p;

    p = version_str;

    if (p == NULL)
	return 0;

    p = strchr(version_str, '.');
    if (p == NULL)
    {
	if (isdigit(*version_str))
	    return encode_version(atoi(version_str), 0, 0, 0, 0, 0, 0);
	return 0;
    }
    backup_whitespace(version_str, p);
    if (!backup_number(version_str, p))
	return 0;

    version = number(p);
    if (version == -1)
	return encode_version(0, major, minor, subminor, year, month, day);
    whitespace(p);
    if (*p != '.')
	return encode_version(version, major, minor, subminor, year, month, day);
    p++;
    whitespace(p);
    major = number(p);
    if (major == -1)
	return encode_version(version, 0, minor, subminor, year, month, day);
    whitespace(p);
    if (*p != '.')
	return encode_version(version, major, minor, subminor, year, month, day);
    p++;
    whitespace(p);
    minor = number(p);
    if (minor == -1)
	return encode_version(version, major, 0, subminor, year, month, day);
    whitespace(p);
    if (*p == '.')
    {
	p++;
	whitespace(p);
	subminor = number(p);
	if (subminor == -1)
	    return encode_version(version, major, minor, 0, year, month, day);
	whitespace(p);
    }
    monthstr[0] = *p;
    p++;
    monthstr[1] = *p;
    p++;
    monthstr[2] = *p;
    p++;
    monthstr[3] = '\0';
    month = month_string_to_number(monthstr);
    if (month == 0)
	return encode_version(version, major, minor, subminor, year, 0, day);
    whitespace(p);
    day = number(p);
    if (day == -1)
	return encode_version(version, major, minor, subminor, year, month, 0);
    whitespace(p);
    year = number(p);
    if (year == -1)
	return encode_version(version, major, minor, subminor, 0, month, day);
    return encode_version(version, major, minor, subminor, year, month, day);
}
