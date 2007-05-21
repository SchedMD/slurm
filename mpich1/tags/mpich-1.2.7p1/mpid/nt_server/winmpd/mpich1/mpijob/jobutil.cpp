#include "mpijob.h"

void GetKeyAndValue(char *str, char *key, char *value)
{
    char *token;

    token = strstr(str, "value=");
    *token = '\0';
    strcpy(value, token+6);
    strcpy(key, str+4);

    // strip any whitespace off the end of the key
    token = &key[strlen(key)-1];
    while (isspace(*token))
	token--;
    token++;
    *token = '\0';
}

bool GetRankAndOption(char *str, int &rank, char *option)
{
    if (str == NULL)
	return false;
    if (!isdigit(*str))
	return false;
    rank = atoi(str);
    while (isdigit(*str))
	str++;
    if (*str == '\0')
	return false;
    strcpy(option, str);
    return true;
}

bool ParseTimeStamp(char *str, int &year, int &month, int &day, int &hour, int &minute, int &second)
{
    char stamp[1024];
    char *token;
    char *seps = ". <>hms";

    strcpy(stamp, str);

    token = strtok(stamp, seps);
    if (token == NULL)
	return false;
    year = atoi(token);
    if (year < 1)
	return false;
    month = day = hour = minute = second = 0;
    token = strtok(NULL, seps);
    if (token == NULL)
	return true;
    month = atoi(token);
    if (month < 1)
	return false;
    token = strtok(NULL, seps);
    if (token == NULL)
	return true;
    day = atoi(token);
    if (day < 1)
	return false;
    token = strtok(NULL, seps);
    if (token == NULL)
	return true;
    hour = atoi(token);
    if (hour < 0)
	return false;
    token = strtok(NULL, seps);
    if (token == NULL)
	return true;
    minute = atoi(token);
    if (minute < 0)
	return false;
    token = strtok(NULL, seps);
    if (token == NULL)
	return true;
    second = atoi(token);
    if (second < 0)
	return false;
    return true;
}

bool CompareTimeStamps(char *t1, char *t2, int &relation)
{
    int yr1, mon1, day1, hr1, min1, sec1;
    int yr2, mon2, day2, hr2, min2, sec2;

    if (!ParseTimeStamp(t1, yr1, mon1, day1, hr1, min1, sec1))
    {
	return false;
    }

    if (!ParseTimeStamp(t2, yr2, mon2, day2, hr2, min2, sec2))
    {
	return false;
    }

    if (yr1 == yr2)
    {
	if (mon1 == mon2)
	{
	    if (day1 == day2)
	    {
		if (hr1 == hr2)
		{
		    if (min1 == min2)
		    {
			if (sec1 == sec2)
			{
			    relation = 0;
			} else if (sec1 < sec2)
			{
			    relation = -1;
			} else
			{
			    relation = 1;
			}
		    } else if (min1 < min2)
		    {
			relation = -1;
		    } else
		    {
			relation = 1;
		    }
		} else if (hr1 < hr2)
		{
		    relation = -1;
		} else
		{
		    relation = 1;
		}
	    } else if (day1 < day2)
	    {
		relation = -1;
	    } else
	    {
		relation = 1;
	    }
	} else if (mon1 < mon2)
	{
	    relation = -1;
	} else
	{
	    relation = 1;
	}
    } else if (yr1 < yr2)
    {
	relation = -1;
    } else
    {
	relation = 1;
    }

    return true;
}
