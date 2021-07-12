/*
 * Copyright 2020 Comcast Cable Communications Management, LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <string.h>
#include "webcfg.h"
#include "webcfg_multipart.h"
#include "webcfg_notify.h"
#include "webcfg_blob.h"
#include "webcfg_generic.h"
#include "webcfg_param.h"
#include "webcfg_timer.h"
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define MIN_MAINTENANCE_TIME		3600					//1hrs in seconds
#define MAX_MAINTENANCE_TIME		14400					//4hrs in seconds
#define MAX_RETRY_TIMEOUT               900
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
static int g_retry_timer = 900;
static long g_retry_timestamp = 0;
static long g_maintenance_time = 0;
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
long get_global_maintenance_time()
{
    return g_maintenance_time;
}

void set_global_maintenance_time(long value)
{
    g_maintenance_time = value;
}

int get_retry_timer()
{
	return g_retry_timer;
}

void set_retry_timer(int value)
{
	g_retry_timer = value;
}

long get_global_retry_timestamp()
{
    return g_retry_timestamp;
}

void set_global_retry_timestamp(long value)
{
    g_retry_timestamp = value;
}

//To print the value in readable format
char* printTime(long long time)
{
	struct tm  ts;
	static char buf[80] = "";

	// Format time, "ddd yymmdd hh:mm:ss zzz"
	time_t rawtime = time;
	ts = *localtime(&rawtime);
	strftime(buf, sizeof(buf), "%a %y%m%d %H:%M:%S", &ts);
	return buf;
}

//To initialize maintenance random timer
void initMaintenanceTimer()
{
	long time_val = 0;
	long fw_start_time = 0;
	long fw_end_time = 0;
	uint16_t random_key = 0;
	char *upgrade_start_time = NULL;
	char *upgrade_end_time = NULL;

	upgrade_start_time = getFirmwareUpgradeStartTime();

	if( upgrade_start_time != NULL )
	{
		WebcfgDebug("upgrade_start_time is %s\n",upgrade_start_time);
		fw_start_time = atoi(upgrade_start_time);
	}
	else
	{
		WebcfgError("Unable to get fw_start_time, use default start time\n");
		fw_start_time = MIN_MAINTENANCE_TIME;
	}
	
	upgrade_end_time = getFirmwareUpgradeEndTime();
	if( upgrade_end_time != NULL )
	{
		WebcfgDebug("upgrade_end_time is %s\n",upgrade_end_time);
		fw_end_time = atoi(upgrade_end_time);
	}
	else
	{
		WebcfgError("Unable to get fw_end_time, use default end time\n");
		fw_end_time = MAX_MAINTENANCE_TIME;
	}

	if( fw_start_time == fw_end_time )
	{
		WebcfgDebug("start and end time values are equal\n");
		fw_start_time = MIN_MAINTENANCE_TIME;
		fw_end_time = MAX_MAINTENANCE_TIME;
	}

	if( fw_start_time > fw_end_time )
	{
		WebcfgDebug("start time is greater than end time\n");
		fw_start_time = fw_start_time - 86400;         //to get a time within the day
	}

        random_key = generateRandomId();
        time_val = (random_key % (fw_end_time - fw_start_time+ 1)) + fw_start_time;

	WebcfgInfo("Firmware Upgrade start time is %ld\n",fw_start_time);
	WebcfgInfo("Firmware Upgrade end time is %ld\n",fw_end_time);

	if( time_val <= 0)
	{
		time_val = time_val + 86400;         //To set a time in next day
	}

	WebcfgDebug("The value of maintenance_time_val is %ld\n",time_val);
	set_global_maintenance_time(time_val);

}

//To Check whether the Maintenance time is at current time
int checkMaintenanceTimer()
{
	struct timespec rt;

	long long cur_time = 0;
	long cur_time_in_sec = 0;

	clock_gettime(CLOCK_REALTIME, &rt);
	cur_time = rt.tv_sec;
	cur_time_in_sec = getTimeInSeconds(cur_time);

	WebcfgDebug("The current time in checkMaintenanceTimer is %lld at %s\n",cur_time, printTime(cur_time));
	WebcfgDebug("The random timer in checkMaintenanceTimer is %ld\n",get_global_maintenance_time());

	if(cur_time_in_sec >= get_global_maintenance_time())
	{
		set_global_supplementarySync(1);
		WebcfgDebug("Maintenance time is equal to current time\n");
		return 1;
	}
	return 0;
}

//To get the wait seconds for Maintenance Time
int getMaintenanceSyncSeconds(int maintenance_count)
{
	struct timespec ct;
	long maintenance_secs = 0;
	long current_time_in_sec = 0;
	long sec_to_12 = 0;
	long long current_time = 0;
	clock_gettime(CLOCK_REALTIME, &ct);

	current_time = ct.tv_sec;
	current_time_in_sec = getTimeInSeconds(current_time);
	maintenance_secs =  get_global_maintenance_time() - current_time_in_sec;

	WebcfgDebug("The current time in maintenanceSyncSeconds is %lld at %s\n",current_time, printTime(current_time));
	WebcfgDebug("The random timer in maintenanceSyncSeconds is %ld\n",get_global_maintenance_time());

	// to shift maintenance sync to next day when already sync happened
	if (maintenance_secs < 0 || maintenance_count == 1 )
	{
		sec_to_12 = 86400 - current_time_in_sec; //Getting the remaining time for midnight 12
		maintenance_secs = sec_to_12 + get_global_maintenance_time();//Adding with Maintenance wait time for nextday trigger
	}

	WebcfgDebug("The maintenance Seconds is %ld\n", maintenance_secs);

	return maintenance_secs;
}

//To get the wait seconds for doc retry
int retrySyncSeconds()
{
	struct timespec ct;
	long retry_secs = 0;
	long current_time_in_sec = 0;
	long long current_time = 0;
	clock_gettime(CLOCK_REALTIME, &ct);

	current_time = ct.tv_sec;
	current_time_in_sec = getTimeInSeconds(current_time);

	retry_secs =  get_global_retry_timestamp() - current_time_in_sec;

	WebcfgDebug("The current time in retrySyncSeconds is %lld at %s\n",current_time, printTime(current_time));
	WebcfgDebug("The random timer in retrySyncSeconds is %ld\n",get_global_retry_timestamp());

	if (retry_secs < 0)
	{
		retry_secs = 0;
	}

	WebcfgDebug("The retry Seconds is %ld\n", retry_secs);

	return retry_secs;
}

//To get the Seconds from Epoch Time
long getTimeInSeconds(long long time)
{
	struct tm cts;
	time_t time_value = time;
	long cur_time_hrs_in_sec = 0;
	long cur_time_min_in_sec = 0;
	long cur_time_sec_in_sec = 0;
	long sec_of_cur_time = 0;

	cts = *localtime(&time_value);

	cur_time_hrs_in_sec = cts.tm_hour * 60 * 60;
	cur_time_min_in_sec = cts.tm_min * 60;
	cur_time_sec_in_sec = cts.tm_sec;

	sec_of_cur_time = cur_time_hrs_in_sec + cur_time_min_in_sec + cur_time_sec_in_sec;

	return sec_of_cur_time;
}

int updateRetryTimeDiff(long long expiry_time)
{
	struct timespec ct;
	int time_diff = 0;
	long long present_time = 0;

	clock_gettime(CLOCK_REALTIME, &ct);
	present_time = ct.tv_sec;

	time_diff = expiry_time - present_time;

	//To set the lowest retry timeout of all the docs
	if(get_retry_timer() > time_diff)
	{
		set_retry_timer(time_diff);
		set_global_retry_timestamp(getTimeInSeconds(expiry_time));
		WebcfgDebug("The retry_timer is %d after set\n", get_retry_timer());
	}
	if(get_global_retry_timestamp() == 0)
	{
		set_global_retry_timestamp(getTimeInSeconds(present_time+900));
	}
	return time_diff;
}

//To set the retry_timestamp to 15 min from current time
long long getRetryExpiryTimeout()
{
	struct timespec ts;
	struct timeval tp;

	gettimeofday(&tp, NULL);

	ts.tv_sec = tp.tv_sec;
	ts.tv_nsec = tp.tv_usec * 1000;

	ts.tv_sec += MAX_RETRY_TIMEOUT;

	return (long long)ts.tv_sec;
}

//To check the timer expiry for retry
int checkRetryTimer( long long timestamp)
{
	struct timespec rt;

	long long cur_time = 0;

	clock_gettime(CLOCK_REALTIME, &rt);
	cur_time = rt.tv_sec;

	WebcfgDebug("The current time in device is %lld at %s\n", cur_time, printTime(cur_time));
	WebcfgDebug("The Retry timestamp is %lld at %s\n", timestamp, printTime(timestamp));

	if(cur_time >= timestamp)
	{
		WebcfgDebug("Retry timestamp is equal to current time\n");
		return 1;
	}
	return 0;
}
