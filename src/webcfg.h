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
#ifndef __WEBCFG_H__
#define __WEBCFG_H__

#include <stdint.h>
#include "webcfg_log.h"
#include <string.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define MAX_BUF_SIZE	           256
#define MAX_PARAMETERNAME_LENGTH       512
#define BACKOFF_SLEEP_DELAY_SEC 	    10

#define WEBCFG_FREE(__x__) if(__x__ != NULL) { free((void*)(__x__)); __x__ = NULL;} else {printf("Trying to free null pointer\n");}
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
extern bool g_shutdown;
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

void processWebconfgSync(char* doc);
int webcfg_http_request(char **configData, int r_count, char* doc, int status, long *code, char **transaction_id,char** contentType, size_t* dataSize);
/**
* @brief webcfgStrncpy WAL String copy function that copies the content of source string into destination string and null terminates the destination string
*
* @param[in] destStr Destination String
* @param[in] srcStr Source String
* @param[in] destSize size of destination string
*/
void webcfgStrncpy(char *destStr, const char *srcStr, size_t destSize);
char* get_global_auth_token();
int Get_PeriodicSyncCheckInterval();
void getCurrent_Time(struct timespec *timer);
long timeVal_Diff(struct timespec *starttime, struct timespec *finishtime);
#endif
