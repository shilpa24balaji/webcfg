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

#include "webcfg_multipart.h"
#include "webcfg_notify.h"
#include "webcfg_generic.h"
#include "webcfg.h"
#include <pthread.h>
#include <cJSON.h>
#include <unistd.h>
#include "webcfg_event.h"
#include "webcfg_db.h"
#include "webcfg_param.h"
#include "webcfg_blob.h"
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
static pthread_t EventThreadId=0;
static pthread_t processThreadId = 0;
pthread_mutex_t event_mut=PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t event_con=PTHREAD_COND_INITIALIZER;
event_data_t *eventDataQ = NULL;
static expire_timer_t * g_timer_head = NULL;
static int numOfEvents = 0;
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
void* blobEventHandler();
int parseEventData();
void* processSubdocEvents();

int checkWebcfgTimer();
int addToEventQueue(char *buf);
void sendSuccessNotification(char *name, uint32_t version);
WEBCFG_STATUS startWebcfgTimer(char *name, uint16_t transID, uint32_t timeout);
WEBCFG_STATUS stopWebcfgTimer(char *name, uint16_t trans_id);
int checkTimerExpired (char **exp_doc);
void createTimerExpiryEvent(char *docName, uint16_t transid);
WEBCFG_STATUS updateTimerList(int status, char *docname, uint16_t transid, uint32_t timeout);
WEBCFG_STATUS deleteFromTimerList(char* doc_name);
WEBCFG_STATUS checkDBVersion(char *docname, uint32_t version);
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
expire_timer_t * get_global_timer_node(void)
{
    return g_timer_head;
}

//Webcfg thread listens for blob events from respective components.
void initEventHandlingTask()
{
	int err = 0;
	err = pthread_create(&EventThreadId, NULL, blobEventHandler, NULL);
	if (err != 0)
	{
		WebcfgError("Error creating Webconfig event handling thread :[%s]\n", strerror(err));
	}
	else
	{
		WebcfgInfo("Webconfig event handling thread created Successfully\n");
	}

}

void* blobEventHandler()
{
	pthread_detach(pthread_self());

	int ret = 0;
	char *expired_doc= NULL;
	uint16_t tx_id = 0;

	ret = registerWebcfgEvent(webcfgCallback);
	if(ret)
	{
		WebcfgInfo("registerWebcfgEvent success\n");
	}
	else
	{
		WebcfgError("registerWebcfgEvent failed\n");
	}

	/* Loop to check timer expiry. When timer is not running, loop is active but wont check expiry until next timer starts. */
	while(1)
	{
		if (checkTimerExpired (&expired_doc))
		{
			if(expired_doc !=NULL)
			{
				WebcfgError("Timer expired_doc %s. No event received within timeout period\n", expired_doc);
				//reset timer. Generate internal EXPIRE event with new trans_id and retry.
				tx_id = generateTransactionId(1001,3000);
				WebcfgInfo("EXPIRE event tx_id generated is %lu\n", (long)tx_id);
				updateTimerList(false, expired_doc, tx_id, 0);
				createTimerExpiryEvent(expired_doc, tx_id);
				WebcfgInfo("After createTimerExpiryEvent\n");
			}
			else
			{
				WebcfgError("Failed to get expired_doc\n");
			}
		}
		else
		{
			WebcfgDebug("Waiting at timer loop of 5s\n");
			sleep(5);
		}
	}
	return NULL;
}

//Call back function to be executed when webconfigSignal signal is received from component.
void webcfgCallback(char *Info, void* user_data)
{
	char *buff = NULL;
	WebcfgInfo("Received webconfig event signal Info %s user_data %s\n", Info, (char*) user_data);
	
	buff = strdup(Info);
	addToEventQueue(buff);

	WebcfgDebug("After addToEventQueue\n");
}

//Producer adds event data to queue
int addToEventQueue(char *buf)
{
	event_data_t *Data;

	WebcfgDebug ("Add data to event queue\n");
        Data = (event_data_t *)malloc(sizeof(event_data_t));

	if(Data)
        {
            Data->data =buf;
            Data->next=NULL;
            pthread_mutex_lock (&event_mut);
            if(eventDataQ == NULL)
            {
                eventDataQ = Data;

                WebcfgInfo("Producer added Data\n");
                pthread_cond_signal(&event_con);
                pthread_mutex_unlock (&event_mut);
                WebcfgDebug("mutex unlock in producer event thread\n");
            }
            else
            {
                event_data_t *temp = eventDataQ;
                while(temp->next)
                {
                    temp = temp->next;
                }
                temp->next = Data;
                pthread_mutex_unlock (&event_mut);
            }
        }
        else
        {
            WebcfgError("failure in allocation for event data\n");
        }

	return 0;
}


//Webcfg consumer thread to process the events.
void processWebcfgEvents()
{
	int err = 0;
	err = pthread_create(&processThreadId, NULL, processSubdocEvents, NULL);
	if (err != 0)
	{
		WebcfgError("Error creating processWebcfgEvents thread :[%s]\n", strerror(err));
	}
	else
	{
		WebcfgInfo("processWebcfgEvents thread created Successfully\n");
	}

}

//Parse and process sub doc event data.
void* processSubdocEvents()
{
	event_params_t *eventParam = NULL;
	int rv = WEBCFG_FAILURE;
	WEBCFG_STATUS uStatus = WEBCFG_FAILURE;
	WEBCFG_STATUS ts = WEBCFG_FAILURE;
	WEBCFG_STATUS rs = WEBCFG_FAILURE;

	while(1)
	{
		pthread_mutex_lock (&event_mut);
		WebcfgDebug("mutex lock in event consumer thread\n");
		if(eventDataQ != NULL)
		{
			event_data_t *Data = eventDataQ;
			eventDataQ = eventDataQ->next;
			pthread_mutex_unlock (&event_mut);
			WebcfgDebug("mutex unlock in event consumer thread\n");

			WebcfgInfo("Data->data is %s\n", Data->data);
			rv = parseEventData(Data->data, &eventParam);
			if(rv == WEBCFG_SUCCESS)
			{
				WebcfgInfo("Event detection\n");
				if (((eventParam->status !=NULL)&&(strcmp(eventParam->status, "ACK")==0)) && (eventParam->timeout == 0))
				{
					WebcfgInfo("ACK event. doc apply success, proceed to add to DB\n");
					//check version for ack, nack is it required?
					ts = stopWebcfgTimer(eventParam->subdoc_name, eventParam->trans_id);
					
					//add to DB, update tmp list and notification based on success ack.

					sendSuccessNotification(eventParam->subdoc_name, eventParam->version);
					WebcfgInfo("AddToDB subdoc_name %s version %lu\n", eventParam->subdoc_name, (long)eventParam->version);
					checkDBList(eventParam->subdoc_name,eventParam->version);
					WebcfgInfo("checkRootUpdate\n");
					if(checkRootUpdate() == WEBCFG_SUCCESS)
					{
						WebcfgInfo("updateRootVersionToDB\n");
						updateRootVersionToDB();
					}
					addNewDocEntry(get_successDocCount());
					WebcfgInfo("After blob addNewDocEntry to DB\n");
				}
				else if (((eventParam->status !=NULL)&&(strcmp(eventParam->status, "NACK")==0)) && (eventParam->timeout == 0))
				{
					WebcfgError("NACK event. doc apply failed for %s\n", eventParam->subdoc_name);
					stopWebcfgTimer(eventParam->subdoc_name, eventParam->trans_id);
					if(ts == WEBCFG_SUCCESS)
					{
						uStatus = updateTmpList(eventParam->subdoc_name, eventParam->version, "failed", "doc_rejected");
						if(uStatus !=WEBCFG_SUCCESS)
						{
							WebcfgError("Failed in updateTmpList for NACK\n");
						}
						WebcfgDebug("get_global_transID is %s\n", get_global_transID());
						addWebConfgNotifyMsg(eventParam->subdoc_name, eventParam->version, "failed", "doc_rejected", get_global_transID(),eventParam->timeout, "status");
					}
				}
				else if ((eventParam->status !=NULL)&&(strcmp(eventParam->status, "EXPIRE")==0))
				{
					WebcfgInfo("EXPIRE event. doc apply timeout expired, need to retry\n");
					WebcfgDebug("get_global_transID is %s\n", get_global_transID());
					addWebConfgNotifyMsg(eventParam->subdoc_name, eventParam->version, "pending", "timer_expired", get_global_transID(),eventParam->timeout, "status");
					WebcfgInfo("retryMultipartSubdoc for EXPIRE case\n");
					retryMultipartSubdoc(eventParam->subdoc_name);
					if(rs == WEBCFG_SUCCESS)
					{
						WebcfgInfo("retryMultipartSubdoc success\n");
					}
					else
					{
						WebcfgError("retryMultipartSubdoc failed\n");
					}
				}
				else if (eventParam->timeout != 0)
				{
					WebcfgInfo("Timeout event. doc apply need time, start timer.\n");
					startWebcfgTimer(eventParam->subdoc_name, eventParam->trans_id, eventParam->timeout);
					addWebConfgNotifyMsg(eventParam->subdoc_name, eventParam->version, "pending", NULL, get_global_transID(),eventParam->timeout, "status");
					WebcfgInfo("After timeout notification\n");
				}
				else
				{
					WebcfgInfo("Crash event. Component restarted after crash, re-send blob.\n");
					addWebConfgNotifyMsg(eventParam->subdoc_name, eventParam->version, "pending", "process_crash", get_global_transID(),eventParam->timeout,"status");

					//If version in event and DB are not matching, re-send blob to retry.
					if(checkDBVersion(eventParam->subdoc_name, eventParam->version) !=WEBCFG_SUCCESS)
					{
						WebcfgInfo("setValues retry for subdoc_name %s\n", eventParam->subdoc_name);
						rs = retryMultipartSubdoc(eventParam->subdoc_name);
						if(rs == WEBCFG_SUCCESS)
						{
							WebcfgInfo("retryMultipartSubdoc success\n");
						}
						else
						{
							WebcfgError("retryMultipartSubdoc failed\n");
						}
					}
					else
					{
						WebcfgInfo("DB and event version are same, retry is not required\n");
					}
				}
			}
			else
			{
				WebcfgError("Failed to parse event Data\n");
			}
			//WEBCFG_FREE(Data);
		}
		else
		{
			WebcfgDebug("Before pthread cond wait in event consumer thread\n");
			pthread_cond_wait(&event_con, &event_mut);
			pthread_mutex_unlock (&event_mut);
			WebcfgDebug("mutex unlock in event consumer thread after cond wait\n");
		}
	}
	return NULL;
}

//Extract values from comma separated string and add to event_params_t structure.
int parseEventData(char* str, event_params_t **val)
{
	event_params_t *param = NULL;
	char *tmpStr =  NULL;
	char * trans_id = NULL;
	char *version = NULL;
	char * timeout =NULL;

	if(str !=NULL)
	{
		param = (event_params_t *)malloc(sizeof(event_params_t));
		if(param)
		{
			memset(param, 0, sizeof(event_params_t));
		        tmpStr = strdup(str);
			//WEBCFG_FREE(str);

		        param->subdoc_name = strsep(&tmpStr, ",");
		        trans_id = strsep(&tmpStr,",");
		        version = strsep(&tmpStr,",");
		        param->status = strsep(&tmpStr,",");
		        timeout = strsep(&tmpStr, ",");

			WebcfgDebug("convert string to uint type\n");
			if(trans_id !=NULL)
			{
				param->trans_id = strtoul(trans_id,NULL,0);
			}

			if(version !=NULL)
			{
				param->version = strtoul(version,NULL,0);
			}

			if(timeout !=NULL)
			{
				param->timeout = strtoul(timeout,NULL,0);
			}

			WebcfgInfo("param->subdoc_name %s param->trans_id %lu param->version %lu param->status %s param->timeout %lu\n", param->subdoc_name, (long)param->trans_id, (long)param->version, param->status, (long)param->timeout);
			*val = param;
			return WEBCFG_SUCCESS;
		}
	}
	return WEBCFG_FAILURE;

}


//To generate custom timer EXPIRE event for expired doc and add to event queue.
void createTimerExpiryEvent(char *docName, uint16_t transid)
{
	char *expiry_event_data = NULL;
	char data[128] = {0};

	snprintf(data,sizeof(data),"%s,%hu,%u,EXPIRE,%u",docName,transid,0,0);
	expiry_event_data = strdup(data);
	WebcfgInfo("expiry_event_data formed %s\n", expiry_event_data);
	if(expiry_event_data)
	{
		addToEventQueue(expiry_event_data);
		WebcfgInfo("Added EXPIRE event queue\n");
	}
	else
	{
		WebcfgError("Failed to generate timer EXPIRE event\n");
	}
}

//Update Tmp list and send success notification to cloud .
void sendSuccessNotification(char *name, uint32_t version)
{
	WEBCFG_STATUS uStatus=1, dStatus=1;

	uStatus = updateTmpList(name, version, "success", "none");
	if(uStatus == WEBCFG_SUCCESS)
	{
		addWebConfgNotifyMsg(name, version, "success", NULL, get_global_transID(),0, "ack");

		dStatus = deleteFromTmpList(name);
		if(dStatus == WEBCFG_SUCCESS)
		{
			WebcfgInfo("blob deleteFromTmpList success\n");
		}
		else
		{
			WebcfgError("blob deleteFromTmpList failed\n");
		}
	}
	else
	{
		WebcfgError("blob updateTmpList failed\n");
	}

}

//start internal timer for required doc when timeout value is received
WEBCFG_STATUS startWebcfgTimer(char *name, uint16_t transID, uint32_t timeout)
{
	if(updateTimerList(true, name, transID, timeout) != WEBCFG_SUCCESS)
	{
		//add docs to timer list
		expire_timer_t *new_node = NULL;
		new_node=(expire_timer_t *)malloc(sizeof(expire_timer_t));

		if(new_node)
		{
			memset( new_node, 0, sizeof( expire_timer_t ) );
			WebcfgDebug("Adding events to list\n");
			new_node->running = true;
			new_node->subdoc_name = strdup(name);
			new_node->txid = transID;
			new_node->timeout = timeout;
			WebcfgDebug("started webcfg internal timer\n");

			new_node->next=NULL;

			if (g_timer_head == NULL)
			{
				g_timer_head = new_node;
			}
			else
			{
				expire_timer_t *temp = NULL;
				WebcfgDebug("Adding next events to list\n");
				temp = g_timer_head;
				while(temp->next !=NULL)
				{
					temp=temp->next;
				}
				temp->next=new_node;
			}

			WebcfgInfo("new_node->subdoc_name %s new_node->txid %lu new_node->timeout %lu status %d added to list\n", new_node->subdoc_name, (long)new_node->txid, (long)new_node->timeout, new_node->running);
			numOfEvents = numOfEvents + 1;
		}
		else
		{
			WebcfgError("Failed in timer allocation\n");
			return WEBCFG_FAILURE;
		}
	}
	WebcfgInfo("startWebcfgTimer success\n");
	return WEBCFG_SUCCESS;
}

//update name, transid, timeout for each doc event
WEBCFG_STATUS updateTimerList(int status, char *docname, uint16_t transid, uint32_t timeout)
{
	expire_timer_t *temp = NULL;
	temp = get_global_timer_node();

	//Traverse through doc list & update required doc timer
	while (NULL != temp)
	{
		WebcfgDebug("node is pointing to temp->subdoc_name %s \n",temp->subdoc_name);
		if( strcmp(docname, temp->subdoc_name) == 0)
		{
			temp->running = status;
			temp->txid = transid;
			temp->timeout = timeout;
			if(strcmp(temp->subdoc_name, docname) !=0)
			{
				WEBCFG_FREE(temp->subdoc_name);
				temp->subdoc_name = NULL;
				temp->subdoc_name = strdup(docname);
			}
			WebcfgInfo("doc timer %s is updated with txid %lu timeout %lu\n", docname, (long)temp->txid, (long)temp->timeout);
			return WEBCFG_SUCCESS;
		}
		temp= temp->next;
	}
	WebcfgDebug("Timer list is empty\n");
	return WEBCFG_FAILURE;
}


//delete doc from webcfg timer list
WEBCFG_STATUS deleteFromTimerList(char* doc_name)
{
	expire_timer_t *prev_node = NULL, *curr_node = NULL;

	if( NULL == doc_name )
	{
		WebcfgError("Invalid value for timer doc\n");
		return WEBCFG_FAILURE;
	}
	WebcfgInfo("timer doc to be deleted: %s\n", doc_name);

	prev_node = NULL;
	curr_node = g_timer_head ;

	// Traverse to get the doc to be deleted
	while( NULL != curr_node )
	{
		if(strcmp(curr_node->subdoc_name, doc_name) == 0)
		{
			WebcfgDebug("Found the node to delete\n");
			if( NULL == prev_node )
			{
				WebcfgDebug("need to delete first doc\n");
				g_timer_head = curr_node->next;
			}
			else
			{
				WebcfgDebug("Traversing to find node\n");
				prev_node->next = curr_node->next;
			}

			WebcfgDebug("Deleting the node entries\n");
			curr_node->running = false;
			curr_node->txid = 0;
			curr_node->timeout = 0;
			WEBCFG_FREE( curr_node->subdoc_name );
			WEBCFG_FREE( curr_node );
			curr_node = NULL;
			WebcfgInfo("Deleted timer successfully and returning..\n");
			numOfEvents =numOfEvents - 1;
			WebcfgInfo("numOfEvents after delete is %d\n", numOfEvents);

			return WEBCFG_SUCCESS;
		}

		prev_node = curr_node;
		curr_node = curr_node->next;
	}
	WebcfgError("Could not find the entry to delete from timer list\n");
	return WEBCFG_FAILURE;
}

//Stop the required doc timer for which ack/nack events received
WEBCFG_STATUS stopWebcfgTimer(char *name, uint16_t trans_id)
{
	expire_timer_t *temp = NULL;
	temp = get_global_timer_node();

	WebcfgInfo("stopWebcfgTimer trans_id %lu\n", (long)trans_id);
	//Traverse through doc list & delete required doc timer from list
	while (NULL != temp)
	{
		WebcfgDebug("node is pointing to temp->subdoc_name %s \n",temp->subdoc_name);
		if( strcmp(name, temp->subdoc_name) == 0)
		{
			if (temp->running)
			{
				if( trans_id ==temp->txid)
				{
					WebcfgInfo("delete timer for sub doc %s\n", name);
					if(deleteFromTimerList(name) == WEBCFG_SUCCESS)
					{
						WebcfgInfo("stopped timer for doc %s\n", name);
						return WEBCFG_SUCCESS;
					}
					else
					{
						WebcfgError("Failed to stop timer for doc %s\n", name);
						break;
					}
				}
				else
				{
					//wait for next event with latest txid T2.
					WebcfgError("temp->txid %lu for doc %s is not matching.\n", (long)temp->txid, name);
					break;
				}
			}
			else
			{
				WebcfgError("timer is not running for doc %s!!!\n", temp->subdoc_name);
				break;
			}
		}
		temp= temp->next;
	}
	WebcfgError("stopWebcfgTimer failed\n");
	return WEBCFG_FAILURE;
}

//check timer expiry by decrementing timeout by 5 for 5s sleep.
int checkTimerExpired (char **exp_doc)
{
	expire_timer_t *temp = NULL;
	temp = get_global_timer_node();

	//Traverse through all docs in list, decrement timer and check if any doc expired.
	while (NULL != temp)
	{
		WebcfgDebug("checking expiry for temp->subdoc_name %s\n",temp->subdoc_name);
		if (temp->running)
		{
			if(temp->timeout == 0)
			{
				WebcfgDebug("Timer Expired for doc %s, doc apply failed\n", temp->subdoc_name);
				*exp_doc = strdup(temp->subdoc_name);
				WebcfgInfo("*exp_doc is %s\n", *exp_doc);
				return true;
			}
			temp->timeout = temp->timeout - 5;
		}
		temp= temp->next;
	}
	return false;
}

WEBCFG_STATUS retryMultipartSubdoc(char *docName)
{
	int i =0, m=0;
	WEBCFG_STATUS rv = WEBCFG_FAILURE;
	param_t *reqParam = NULL;
	WDMP_STATUS ret = WDMP_FAILURE;
	int ccspStatus=0;
	int paramCount = 0;
	webcfgparam_t *pm = NULL;
	multipart_t *gmp = NULL;

	gmp = get_global_mp();

	if(gmp ==NULL)
	{
		WebcfgError("Multipart mp cache is NULL\n");
		return rv;
	}

	for(m = 0 ; m<((int)gmp->entries_count)-1; m++)
	{
		if(strcmp(gmp->entries[m].name_space, docName) == 0)
		{
			WebcfgInfo("gmp->entries[%d].name_space %s\n", m, gmp->entries[m].name_space);
			WebcfgInfo("gmp->entries[%d].etag %lu\n" ,m,  (long)gmp->entries[m].etag);
			WebcfgDebug("gmp->entries[%d].data %s\n" ,m,  gmp->entries[m].data);
			WebcfgInfo("gmp->entries[%d].data_size is %zu\n", m,gmp->entries[m].data_size);

			WebcfgInfo("--------------decode root doc-------------\n");
			pm = webcfgparam_convert( gmp->entries[m].data, gmp->entries[m].data_size+1 );
			if ( NULL != pm)
			{
				paramCount = (int)pm->entries_count;

				reqParam = (param_t *) malloc(sizeof(param_t) * paramCount);
				memset(reqParam,0,(sizeof(param_t) * paramCount));

				WebcfgInfo("paramCount is %d\n", paramCount);
				for (i = 0; i < paramCount; i++)
				{
			                if(pm->entries[i].value != NULL)
			                {
						if(pm->entries[i].type == WDMP_BLOB)
						{
							char *appended_doc = NULL;
							WebcfgInfo("B4 webcfg_appendeddoc\n");
							appended_doc = webcfg_appendeddoc( gmp->entries[m].name_space, gmp->entries[m].etag, pm->entries[i].value, pm->entries[i].value_size);
							reqParam[i].name = strdup(pm->entries[i].name);
							WebcfgInfo("appended_doc length: %zu\n", strlen(appended_doc));
							reqParam[i].value = strdup(appended_doc);
							reqParam[i].type = WDMP_BASE64;
							WEBCFG_FREE(appended_doc);
							WebcfgInfo("appended_doc done\n");
						}
						else
						{
							WebcfgError("blob type is incorrect\n");
						}
			                }
					WebcfgInfo("Request:> param[%d].name = %s, type = %d\n",i,reqParam[i].name,reqParam[i].type);
					WebcfgInfo("Request:> param[%d].value = %s\n",i,reqParam[i].value);
					WebcfgInfo("Request:> param[%d].type = %d\n",i,reqParam[i].type);
				}

				WebcfgInfo("Proceed to setValues..\n");
				if(reqParam !=NULL)
				{
					WebcfgInfo("retryMultipartSubdoc WebConfig SET Request\n");
					setValues(reqParam, paramCount, ATOMIC_SET_WEBCONFIG, NULL, NULL, &ret, &ccspStatus);
					if(ret == WDMP_SUCCESS)
					{
						WebcfgInfo("retryMultipartSubdoc setValues success. ccspStatus : %d\n", ccspStatus);
						rv = WEBCFG_SUCCESS;
					}
					else
					{
						WebcfgError("retryMultipartSubdoc setValues Failed. ccspStatus : %d\n", ccspStatus);
					}
					WebcfgInfo("reqParam_destroy\n");
					reqParam_destroy(paramCount, reqParam);
				}
				WebcfgInfo("webcfgparam_destroy\n");
				webcfgparam_destroy( pm );
			}
			else
			{
				WebcfgError("--------------decode root doc failed-------------\n");
			}
			break;
		}
		else
		{
			WebcfgError("docName %s not found in mp list\n", docName);
		}
	}
	return rv;
}

WEBCFG_STATUS checkDBVersion(char *docname, uint32_t version)
{
	webconfig_db_data_t *webcfgdb = NULL;
	webcfgdb = get_global_db_node();

	//Traverse through doc list & check version for required doc
	while (NULL != webcfgdb)
	{
		WebcfgDebug("node is pointing to webcfgdb->name %s, docname %s, webcfgdb->version %lu, version %lu \n",webcfgdb->name, docname, (long)webcfgdb->version, (long)version);
		if( strcmp(docname, webcfgdb->name) == 0)
		{
			if(webcfgdb->version == version)
			{
				WebcfgInfo("webcfgdb version is same for doc %s\n", docname);
				return WEBCFG_SUCCESS;
			}
		}
		webcfgdb= webcfgdb->next;
	}
	return WEBCFG_FAILURE;
}
