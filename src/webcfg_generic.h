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
#ifndef __WEBCFGGENERIC_H__
#define __WEBCFGGENERIC_H__

#include <stdint.h>
#include <wdmp-c.h>

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/


/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
/* return logger file name */
const char *fetch_generic_file(void);
void _get_webCfg_interface(char **interface);
int _getConfigVersion(int index, char **version);
bool _getConfigURL(int index, char **url);
bool _getRequestTimeStamp(int index,char **RequestTimeStamp);
int _setRequestTimeStamp(int index);
int _setSyncCheckOK(int index, bool status);
int _setConfigVersion(int index, char *version);
int setForceSync(char* pString, char *transactionId,int *session_status);
int getForceSync(char** pString, char **transactionId);

char *get_global_systemReadyTime();
char* getDeviceBootTime();
char * getSerialNumber();
char * getProductClass();
char * getModelName();
char * getFirmwareVersion();
void getDeviceMac();
char* get_global_deviceMAC();

void initWebConfigMultipartTask();
void sendNotification(char *payload, char *source, char *destination);
/**
 * @brief setValues interface sets the parameter value.
 *
 * @param[in] paramVal List of Parameter name/value pairs.
 * @param[in] paramCount Number of parameters.
 * @param[in] setType Flag to specify the type of set operation.
 * @param[out] timeSpan timing_values for each component. 
 * @param[out] retStatus Returns status
 * @param[out] ccspStatus Returns ccsp set status
 */
void setValues(const param_t paramVal[], const unsigned int paramCount, const int setType, char *transactionId, money_trace_spans *timeSpan, WDMP_STATUS *retStatus, int *ccspStatus);

#endif
