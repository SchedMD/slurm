#include "GetCPUsage.h"
#include <pdh.h>
#include <stdio.h>

HQUERY g_hQuery;
HCOUNTER g_hCounter;
bool g_bInitCPUsageSuccessful = false;

void InitCPUsage()
{
	int error;
	try {
		if (g_bInitCPUsageSuccessful)
			return;

		if (PdhOpenQuery(NULL, 1, &g_hQuery) != ERROR_SUCCESS)
		{
			error = GetLastError();
			printf("PdhOpenQuery failed, %d\n", error);
			return;
		}
		
		// Add the counter to the current query
		if (PdhAddCounter(g_hQuery, "\\Processor(_Total)\\% Processor Time", 0, &g_hCounter) != ERROR_SUCCESS) 
		{
			printf("Failed to PdhAddCounter.\n");
			PdhCloseQuery(g_hQuery);
			return;
		}

		g_bInitCPUsageSuccessful = true;

	} catch(...)
	{
		printf("Exception in InitCPUsage\n");
	}
}

void CleanupCPUsage()
{
	if (g_bInitCPUsageSuccessful)
		PdhCloseQuery(g_hQuery);
}

int GetCPUsage()
{
	int error;
	PDH_FMT_COUNTERVALUE value;

	try {
		if (!g_bInitCPUsageSuccessful)
			InitCPUsage();
		if (!g_bInitCPUsageSuccessful)
			return 0;

		// Get the formatted data
		if (PdhCollectQueryData(g_hQuery) != ERROR_SUCCESS)
		{
			error = GetLastError();
			printf("PdhCollectQueryData failed, %d\n", error);
			PdhCloseQuery(g_hQuery);
			g_bInitCPUsageSuccessful = false;
			return 0;
		}
		else
		{
			if (PdhGetFormattedCounterValue(g_hCounter, PDH_FMT_LONG, NULL, &value) == ERROR_SUCCESS)
			{
				if (value.CStatus == ERROR_SUCCESS)
					return value.longValue;
			}
		}
	} catch(...)
	{
		printf("Exception in GetCPUsage()\n");
	}
	
	return 0;
}
