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
#include "multipart.h"
#include "webcfg_notify.h"
#include "webcfg_generic.h"
#include "webcfgparam.h"
#include "webcfg_auth.h"
#include <msgpack.h>
#include <wdmp-c.h>
#include <base64.h>
#include "macbindingdoc.h"
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
pthread_mutex_t periodicsync_mutex=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t periodicsync_condition=PTHREAD_COND_INITIALIZER;
bool g_shutdown  = false;
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
void *WebConfigMultipartTask();
int handlehttpResponse(long response_code, char *webConfigData, int retry_count, char* doc, char* transaction_uuid, char* ct, size_t dataSize);
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

void initWebConfigMultipartTask()
{
	int err = 0;
	pthread_t threadId;

	err = pthread_create(&threadId, NULL, WebConfigMultipartTask, NULL);
	if (err != 0) 
	{
		WebConfigLog("Error creating WebConfigMultipartTask thread :[%s]\n", strerror(err));
	}
	else
	{
		WebConfigLog("WebConfigMultipartTask Thread created Successfully.\n");
	}
}

void *WebConfigMultipartTask()
{
	pthread_detach(pthread_self());
	int ret = 0;
	int rv=0;
        struct timeval tp;
        struct timespec ts;
        time_t t;
	int wait_flag=0;
	int forced_sync=0;
	char *syncDoc = NULL;
        int value=0;
	WebConfigLog("Inside WebConfigMultipartTask.\n");
	value =Get_PeriodicSyncCheckInterval();

	//start webconfig notification thread.
	initWebConfigNotifyTask();
	WebConfigLog("After initWebConfigNotifyTask\n");
	while(1)
	{
		if(forced_sync)
		{
			//trigger sync only for particular doc
			WebConfigLog("Trigger Forced sync for doc %s\n", syncDoc);
			processWebconfgSync(syncDoc);
			WebConfigLog("reset forced_sync and syncDoc after sync\n");
			forced_sync = 0;
			WEBCFG_FREE(syncDoc);
			syncDoc = NULL;
			WebConfigLog("reset ForceSyncCheck after sync\n");
			setForceSync("", "", 0);
		}
		else if(!wait_flag)
		{
			WebConfigLog("B4 processWebconfgSync\n");
			processWebconfgSync(syncDoc);
		}

		pthread_mutex_lock (&periodicsync_mutex);
		gettimeofday(&tp, NULL);
		ts.tv_sec = tp.tv_sec;
		ts.tv_nsec = tp.tv_usec * 1000;
		ts.tv_sec += value;

		if (g_shutdown)
		{
			WebConfigLog("g_shutdown is %d, proceeding to kill webconfig thread\n", g_shutdown);
			pthread_mutex_unlock (&periodicsync_mutex);
			break;
		}
		value=Get_PeriodicSyncCheckInterval();
		WebConfigLog("PeriodicSyncCheckInterval value is %d\n",value);
		if(value == 0)
		{
			WebcfgDebug("B4 periodicsync_condition pthread_cond_wait\n");
			pthread_cond_wait(&periodicsync_condition, &periodicsync_mutex);
			rv =0;
		}
		else
		{
			WebcfgDebug("B4 periodicsync_condition pthread_cond_timedwait\n");
			rv = pthread_cond_timedwait(&periodicsync_condition, &periodicsync_mutex, &ts);
		}

		if(!rv && !g_shutdown)
		{
			time(&t);
			char *ForceSyncDoc = NULL;
			char* ForceSyncTransID = NULL;

			// Identify comma separated doc name which needs ForceSync
			getForceSync(&ForceSyncDoc, &ForceSyncTransID);
			WebConfigLog("ForceSyncDoc %s ForceSyncTransID. %s\n", ForceSyncDoc, ForceSyncTransID);
			if(ForceSyncTransID !=NULL)
			{
				if((ForceSyncDoc != NULL) && strlen(ForceSyncDoc)>0)
				{
					//if(ForceSyncEnable)
					wait_flag=0;
					forced_sync = 1;
					//syncIndex = index;
					syncDoc = strdup(ForceSyncDoc);
					WebConfigLog("Received signal interrupt to getForceSync at %s\n",ctime(&t));
					WEBCFG_FREE(ForceSyncDoc);
					WEBCFG_FREE(ForceSyncTransID);
				}
				else
				{
					WebConfigLog("ForceSyncDoc is NULL\n");
					WEBCFG_FREE(ForceSyncTransID);
				}
			}

			WebConfigLog("forced_sync is %d\n", forced_sync);
			if(!forced_sync)
			{
				wait_flag=1;
				value=Get_PeriodicSyncCheckInterval();
				WebConfigLog("Received signal interrupt to change the sync interval to %d\n",value);
			}
		}
		else if (rv == ETIMEDOUT && !g_shutdown)
		{
			time(&t);
			wait_flag=0;
			WebConfigLog("Periodic Sync Interval %d sec and syncing at %s\n",value,ctime(&t));
		}
		else if(g_shutdown)
		{
			WebConfigLog("Received signal interrupt to RFC disable. g_shutdown is %d, proceeding to kill webconfig thread\n", g_shutdown);
			pthread_mutex_unlock (&periodicsync_mutex);
			break;
		}
		pthread_mutex_unlock(&periodicsync_mutex);

	}
	if(get_global_notify_threadid())
	{
		ret = pthread_join (get_global_notify_threadid(), NULL);
		if(ret ==0)
		{
			WebConfigLog("pthread_join returns success\n");
		}
		else
		{
			WebConfigLog("Error joining thread\n");
		}
	}
	WebConfigLog("B4 pthread_exit\n");
	pthread_exit(0);
	WebcfgDebug("After pthread_exit\n");
	return NULL;
}

/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/
void processWebconfgSync(char* doc)
{
	int retry_count=0;
	int r_count=0;
	int configRet = -1;
	char *webConfigData = NULL;
	long res_code;
	int rv=0;
	int status=0;
	char *transaction_uuid =NULL;
	char *ct=NULL;
	size_t dataSize=0;

	WebConfigLog("========= Start of processWebconfgSync =============\n");
	while(1)
	{
		if(retry_count >3)
		{
			WebConfigLog("retry_count has reached max limit. Exiting.\n");
			retry_count=0;
			break;
		}
		WebConfigLog("doc is %s\n", doc);
		configRet = webcfg_http_request(&webConfigData, r_count, doc, status, &res_code, &transaction_uuid, &ct, &dataSize);
		printf("After webcfg_http_request ct is %s\n", ct );
		if(configRet == 0)
		{
			WebConfigLog("B4 handlehttpResponse\n");
			rv = handlehttpResponse(res_code, webConfigData, retry_count, doc, transaction_uuid, ct, dataSize);
			if(rv ==1)
			{
				WebConfigLog("No retries are required. Exiting..\n");
				break;
			}
		}
		else
		{
			WebConfigLog("Failed to get webConfigData from cloud\n");
		}
		WebConfigLog("webcfg_http_request BACKOFF_SLEEP_DELAY_SEC is %d seconds\n", BACKOFF_SLEEP_DELAY_SEC);
		sleep(BACKOFF_SLEEP_DELAY_SEC);
		retry_count++;
		WebConfigLog("Webconfig retry_count is %d\n", retry_count);
	}
	WebcfgDebug("========= End of processWebconfgSync =============\n");
	return;
}

int handlehttpResponse(long response_code, char *webConfigData, int retry_count, char* doc, char* transaction_uuid, char *ct, size_t dataSize)
{
	int first_digit=0;
	int msgpack_status=0;
	int setRet = 0;
	int ret=0, getRet = 0;
	char *configURL = NULL;
	char *configVersion = NULL;
	char *RequestTimeStamp=NULL;
	char *newDocVersion = NULL;
	int err = 0;
	int index =0 ;//remove this.

        //get common items for all status codes and send notification.
        //getRet = _getConfigURL(index, &configURL); //remove the index and add the sub doc names
	if(getRet)
	{
		WebcfgDebug("configURL for index %d is %s\n", index, configURL);
	}
	else
	{
		WebConfigLog("getConfigURL failed for index %d\n", index);
	}
	WebConfigLog("doc is %s\n", doc);
	//getRet = _getConfigVersion(index, &configVersion);
	if(getRet)
	{
		WebConfigLog("configVersion for index %d is %s\n", index, configVersion);
	}
	else
	{
		WebConfigLog("getConfigVersion failed for index %d\n", index);
	}

        //ret = _setRequestTimeStamp(index);
	if(ret == 0)
	{
		WebcfgDebug("RequestTimeStamp set successfully for index %d\n", index);
	}
	else
	{
		WebConfigLog("Failed to set RequestTimeStamp for index %d\n", index);
	}

        //getRet = _getRequestTimeStamp(index, &RequestTimeStamp);
	if(getRet)
	{
		WebcfgDebug("RequestTimeStamp for index %d is %s\n", index, RequestTimeStamp);
	}
	else
	{
		WebConfigLog("RequestTimeStamp get failed for index %d\n", index);
	}

	if(response_code == 304)
	{
		WebConfigLog("webConfig is in sync with cloud. response_code:%ld\n", response_code);
		//_setSyncCheckOK(index, true);
		addWebConfigNotifyMsg(configURL, response_code, NULL, 0, RequestTimeStamp , configVersion, transaction_uuid);
		return 1;
	}
	else if(response_code == 200)
	{
		WebConfigLog("webConfig is not in sync with cloud. response_code:%ld\n", response_code);

		if(webConfigData !=NULL)
		{
			WebcfgDebug("webConfigData fetched successfully\n");
			WebConfigLog("parseMultipartDocument\n");
			msgpack_status = parseMultipartDocument(webConfigData, ct, dataSize);
			WebConfigLog("setRet after process msgPack is %d\n", setRet);
			WebcfgDebug("newDocVersion is %s\n", newDocVersion);

			if(msgpack_status == 1)
			{
				WebcfgDebug("parseMultipartDocument success\n");
				if(configURL!=NULL && newDocVersion !=NULL)
				{
					WebConfigLog("Configuration settings from %s version %s were applied successfully\n", configURL, newDocVersion );
				}

				WebcfgDebug("set version and syncCheckOK for success\n");
				//ret = _setConfigVersion(index, newDocVersion);//get new version from msgpack doc
				if(ret == 0)
				{
					WebcfgDebug("Config Version %s set successfully for index %d\n", newDocVersion, index);
				}
				else
				{
					WebConfigLog("Failed to set Config version %s for index %d\n", newDocVersion, index);
				}

				//ret = _setSyncCheckOK(index, true );
				if(ret == 0)
				{
					WebcfgDebug("SyncCheckOK set successfully for index %d\n", index);
				}
				else
				{
					WebConfigLog("Failed to set SyncCheckOK for index %d\n", index);
				}

				addWebConfigNotifyMsg(configURL, response_code, "success", setRet, RequestTimeStamp , newDocVersion, transaction_uuid);
				return 1;
			}
			else
			{
				WebConfigLog("Failure in parseMultipartDocument\n");
				//ret = _setSyncCheckOK(index, false);
				if(ret == 0)
				{
					WebcfgDebug("SyncCheckOK set to false for index %d\n", index);
				}
				else
				{
					WebConfigLog("Failed to set SyncCheckOK to false for index %d\n", index);
				}

				WebConfigLog("Configuration settings from %s version %s FAILED\n", configURL, newDocVersion );
				WebConfigLog("Sending Webconfig apply Failure Notification\n");
				addWebConfigNotifyMsg(configURL, response_code, "failed", setRet, RequestTimeStamp , newDocVersion, transaction_uuid);
				return 1;
			}
		}
		else
		{
			WebConfigLog("webConfigData is empty, need to retry\n");
		}
	}
	else if(response_code == 204)
	{
		WebConfigLog("No configuration available for this device. response_code:%ld\n", response_code);
		addWebConfigNotifyMsg(configURL, response_code, NULL, 0, RequestTimeStamp , configVersion, transaction_uuid);
		return 1;
	}
	else if(response_code == 403)
	{
		WebConfigLog("Token is expired, fetch new token. response_code:%ld\n", response_code);
		createNewAuthToken(get_global_auth_token(), sizeof(get_global_auth_token()), get_global_deviceMAC(), get_global_serialNum() );
		WebcfgDebug("createNewAuthToken done in 403 case\n");
		err = 1;
	}
	else if(response_code == 429)
	{
		WebConfigLog("No action required from client. response_code:%ld\n", response_code);
		WEBCFG_FREE(configURL);
		WEBCFG_FREE(configVersion);
		WEBCFG_FREE(RequestTimeStamp);
		WEBCFG_FREE(transaction_uuid);
		return 1;
	}
	first_digit = (int)(response_code / pow(10, (int)log10(response_code)));
	if((response_code !=403) && (first_digit == 4)) //4xx
	{
		WebConfigLog("Action not supported. response_code:%ld\n", response_code);
		addWebConfigNotifyMsg(configURL, response_code, NULL, 0, RequestTimeStamp , configVersion, transaction_uuid);
		return 1;
	}
	else //5xx & all other errors
	{
		WebConfigLog("Error code returned, need to retry. response_code:%ld\n", response_code);
		if(retry_count == 3 && !err)
		{
			WebcfgDebug("Sending Notification after 3 retry attempts\n");
			addWebConfigNotifyMsg(configURL, response_code, NULL, 0, RequestTimeStamp , configVersion, transaction_uuid);
			return 0;
		}
		WEBCFG_FREE(configURL);
		WEBCFG_FREE(configVersion);
		WEBCFG_FREE(RequestTimeStamp);
		WEBCFG_FREE(transaction_uuid);
	}
	return 0;
}

void webcfgStrncpy(char *destStr, const char *srcStr, size_t destSize)
{
    strncpy(destStr, srcStr, destSize-1);
    destStr[destSize-1] = '\0';
}

void getCurrent_Time(struct timespec *timer)
{
	clock_gettime(CLOCK_REALTIME, timer);
}

long timeVal_Diff(struct timespec *starttime, struct timespec *finishtime)
{
	long msec;
	msec=(finishtime->tv_sec-starttime->tv_sec)*1000;
	msec+=(finishtime->tv_nsec-starttime->tv_nsec)/1000000;
	return msec;
}

int Get_PeriodicSyncCheckInterval()
{
	return 0;
}

