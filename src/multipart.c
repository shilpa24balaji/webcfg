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
#include <string.h>
#include <stdlib.h>
#include "multipart.h"
#include "webcfgparam.h"
#include "webcfg_log.h"
#include "webcfg_auth.h"
#include "webcfg_generic.h"
#include <uuid/uuid.h>
#include "macbindingdoc.h"
#include "portmappingdoc.h"
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define MAX_HEADER_LEN			4096
#define ETAG_HEADER 		       "Etag:"
#define CURL_TIMEOUT_SEC	   25L
#define CA_CERT_PATH 		   "/etc/ssl/certs/ca-certificates.crt"
#define WEBPA_READ_HEADER          "/etc/parodus/parodus_read_file.sh"
#define WEBPA_CREATE_HEADER        "/etc/parodus/parodus_create_file.sh"
#define WEBCFG_URL_FILE 	   "/tmp/webcfg_url" //check here.
/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
struct token_data {
    size_t size;
    char* data;
};

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
static char g_ETAG[64]={'\0'};
char webpa_aut_token[4096]={'\0'};
static char g_interface[32]={'\0'};
static char g_systemReadyTime[64]={'\0'};
static char g_FirmwareVersion[64]={'\0'};
static char g_bootTime[64]={'\0'};
static char g_productClass[64]={'\0'};
static char g_ModelName[64]={'\0'};
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
size_t writer_callback_fn(void *buffer, size_t size, size_t nmemb, struct token_data *data);
size_t headr_callback(char *buffer, size_t size, size_t nitems);
void stripspaces(char *str, char **final_str);
void createCurlHeader( struct curl_slist *list, struct curl_slist **header_list, int status, char *doc, char ** trans_uuid);
void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m, int *no_of_subdocbytes);
void multipart_destroy( multipart_t *m );
char* generate_trans_uuid();
int processMsgpackSubdoc(multipart_t *mp);
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
/*
* @brief Initialize curl object with required options. create configData using libcurl.
* @param[out] configData
* @param[in] len total configData size
* @param[in] r_count Number of curl retries on ipv4 and ipv6 mode during failure
* @return returns 0 if success, otherwise failed to fetch auth token and will be retried.
*/
int webcfg_http_request(char **configData, int r_count, char* doc, int status, long *code, char **transaction_id, char** contentType, size_t *dataSize)
{
	CURL *curl;
	CURLcode res;
	CURLcode time_res;
	struct curl_slist *list = NULL;
	struct curl_slist *headers_list = NULL;
	int rv=1;
	double total;
	long response_code = 0;
	char *interface = NULL;
	char *ct = NULL;
	char *webConfigURL = NULL;
	int len=0;
	char *transID = NULL;

	int content_res=0;
	struct token_data data;
	data.size = 0;
	void * dataVal = NULL;
	char syncURL[256]={'\0'};

	WebConfigLog("Inside webcfg_http_request.\n");
	curl = curl_easy_init();
	if(curl)
	{
		//this memory will be dynamically grown by write call back fn as required
		data.data = (char *) malloc(sizeof(char) * 1);
		if(NULL == data.data)
		{
			WebConfigLog("Failed to allocate memory.\n");
			return rv;
		}
		data.data[0] = '\0';
		createCurlHeader(list, &headers_list, status, doc, &transID);
		if(transID !=NULL)
		{
			*transaction_id = strdup(transID);
			WEBCFG_FREE(transID);
		}
		WebConfigLog("webConfigURL readFromFile\n");
		readFromFile(WEBCFG_URL_FILE, &webConfigURL, &len );

		//Update query param in the URL based on the doc name for force sync
		if (doc != NULL)
		{
			WebConfigLog("update webConfigURL based on sync doc\n");
			snprintf(syncURL, MAX_BUF_SIZE, "%s?group_id=%s", webConfigURL, doc);
			WebConfigLog("syncURL is %s\n", syncURL);
			webConfigURL =strdup( syncURL);
		}

		if(webConfigURL !=NULL)
		{
			WebConfigLog("webconfig root ConfigURL is %s\n", webConfigURL);
			curl_easy_setopt(curl, CURLOPT_URL, webConfigURL );
		}
		else
		{
			WebConfigLog("Failed to get webconfig configURL\n");
			return rv;
		}
		curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SEC);
		WebConfigLog("fetching interface from device.properties\n");
		if(strlen(g_interface) == 0)
		{
			//get_webCfg_interface(&interface);
			interface = strdup("erouter0"); //check here.
			if(interface !=NULL)
		        {
		               strncpy(g_interface, interface, sizeof(g_interface)-1);
		               WebcfgDebug("g_interface copied is %s\n", g_interface);
		               WEBCFG_FREE(interface);
		        }
		}
		WebConfigLog("g_interface fetched is %s\n", g_interface);
		if(strlen(g_interface) > 0)
		{
			WebcfgDebug("setting interface %s\n", g_interface);
			curl_easy_setopt(curl, CURLOPT_INTERFACE, g_interface);
		}

		// set callback for writing received data
		dataVal = &data;
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer_callback_fn);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, dataVal);

		curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);

		curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headr_callback);

		// setting curl resolve option as default mode.
		//If any failure, retry with v4 first and then v6 mode.
		if(r_count == 1)
		{
			WebConfigLog("curl Ip resolve option set as V4 mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		}
		else if(r_count == 2)
		{
			WebConfigLog("curl Ip resolve option set as V6 mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
		}
		else
		{
			WebConfigLog("curl Ip resolve option set as default mode\n");
			curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
		}
		curl_easy_setopt(curl, CURLOPT_CAINFO, CA_CERT_PATH);
		// disconnect if it is failed to validate server's cert
		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		// Verify the certificate's name against host
  		curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		// To use TLS version 1.2 or later
  		curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
		// To follow HTTP 3xx redirections
  		curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// Perform the request, res will get the return code
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		WebConfigLog("webConfig curl response %d http_code %ld\n", res, response_code);
		*code = response_code;
		time_res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
		if(time_res == 0)
		{
			WebConfigLog("curl response Time: %.1f seconds\n", total);
		}
		curl_slist_free_all(headers_list);
		WebConfigLog("free webConfigURL\n");
		WEBCFG_FREE(webConfigURL);
		WebConfigLog("After free webConfigURL\n");
		if(res != 0)
		{
			WebConfigLog("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
		else
		{
			WebConfigLog("checking content type\n");
			content_res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
			WebConfigLog("ct is %s, content_res is %d\n", ct, content_res);
			*configData=data.data;
			WebConfigLog("Data size is : %d\n",(int)data.size);
			*contentType = strdup(ct);
			*dataSize = data.size;
		}
		//WEBCFG_FREE(data.data);
		curl_easy_cleanup(curl);
		rv=0;
	}
	else
	{
		WebConfigLog("curl init failure\n");
	}
	return rv;
}

int parseMultipartDocument(void *config_data, char *ct , size_t data_size)
{
	char *boundary = NULL;
	char *str=NULL;
	char *line_boundary = NULL;
	char *last_line_boundary = NULL;
	char *str_body = NULL;
	multipart_t *mp = NULL;
	int subdocbytes =0;
	int boundary_len =0;
	int rv = -1,count =0;
	
	printf("ct is %s\n", ct );
	// fetch boundary
	str = strtok(ct,";");
	str = strtok(NULL, ";");
	boundary= strtok(str,"=");
	boundary= strtok(NULL,"=");
	WebConfigLog( "boundary %s\n", boundary );
	
	if(boundary !=NULL)
	{
		boundary_len= strlen(boundary);
	}

	line_boundary  = (char *)malloc(sizeof(char) * (boundary_len +5));
	snprintf(line_boundary,boundary_len+5,"--%s\r\n",boundary);
	WebConfigLog( "line_boundary %s, len %zu\n", line_boundary, strlen(line_boundary) );

	last_line_boundary  = (char *)malloc(sizeof(char) * (boundary_len + 5));
	snprintf(last_line_boundary,boundary_len+5,"--%s--",boundary);
	WebConfigLog( "last_line_boundary %s, len %zu\n", last_line_boundary, strlen(last_line_boundary) );
	// Use --boundary to split
	str_body = malloc(sizeof(char) * data_size + 1);
	str_body = memcpy(str_body, config_data, data_size + 1);
	int num_of_parts = 0;
	char *ptr_lb=str_body;
	char *ptr_lb1=str_body;
	char *ptr_count = str_body;
	int index1=0, index2 =0 ;

	/* For Subdocs count */
	while((ptr_count - str_body) < (int)data_size )
	{
		if(0 == memcmp(ptr_count, last_line_boundary, strlen(last_line_boundary)))
		{
			num_of_parts++;
			break;
		}
		else if(0 == memcmp(ptr_count, line_boundary, strlen(line_boundary)))
		{
			num_of_parts++;
		}
		ptr_count++;
	}
	WebConfigLog("Size of the docs is :%d\n", (num_of_parts-1));
	/* For Subdocs count */

	mp = (multipart_t *) malloc (sizeof(multipart_t));
	mp->entries_count = (size_t)num_of_parts;
	mp->entries = (multipartdocs_t *) malloc(sizeof(multipartdocs_t )*(mp->entries_count-1) );
	memset( mp->entries, 0, sizeof(multipartdocs_t)*(mp->entries_count-1));
	///Scanning each lines with \n as delimiter
	while((ptr_lb - str_body) < (int)data_size)
	{
		if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
		{
			WebConfigLog("last line boundary \n");
			break;
		}
		if (0 == memcmp(ptr_lb, "-", 1) && 0 == memcmp(ptr_lb, line_boundary, strlen(line_boundary)))
		{
			ptr_lb = ptr_lb+(strlen(line_boundary));
			num_of_parts = 1;
			while(0 != num_of_parts % 2)
			{
				ptr_lb = memchr(ptr_lb, '\n', data_size - (ptr_lb - str_body));
				// printf("printing newline: %ld\n",ptr_lb-str_body);
				ptr_lb1 = memchr(ptr_lb+1, '\n', data_size - (ptr_lb - str_body));
				// printf("printing newline2: %ld\n",ptr_lb1-str_body);
				if(0 != memcmp(ptr_lb1-1, "\r",1 )){
				ptr_lb1 = memchr(ptr_lb1+1, '\n', data_size - (ptr_lb - str_body));
				}
				index2 = ptr_lb1-str_body;
				index1 = ptr_lb-str_body;
				parse_multipart(str_body+index1+1,index2 - index1 - 2, &mp->entries[count],&subdocbytes);
				ptr_lb++;

				if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
				{
					WebConfigLog("last line boundary inside \n");
					break;
				}
				if(0 == memcmp(ptr_lb1+1, "-", 1) && 0 == memcmp(ptr_lb1+1, line_boundary, strlen(line_boundary)))
				{
					WebConfigLog(" line boundary inside \n");
					num_of_parts++;
					count++;
				}
			}
		}
		else
		{
			ptr_lb++;
		}
	}

	int status =0;
	status = processMsgpackSubdoc(mp);
	if(status ==0)
	{
		WebConfigLog("processMsgpackSubdoc success\n");
		rv =1;
	}
	else
	{
		WebConfigLog("processMsgpackSubdoc failed\n");	
	}
	return rv;
}

int processMsgpackSubdoc(multipart_t *mp)
{
	int i =0, m=0;
	int rv = -1;
	param_t *reqParam = NULL;
	WDMP_STATUS ret = WDMP_FAILURE;
	int ccspStatus=0;
	int paramCount = 0;
	webcfgparam_t *pm;

	for(m = 0 ; m<((int)mp->entries_count-1); m++)
	{
		WebConfigLog("mp->entries[%d].name_space %s\n", m, mp->entries[m].name_space);
		WebConfigLog("mp->entries[%d].etag %s\n" ,m,  mp->entries[m].etag);
		WebConfigLog("mp->entries[%d].data %s\n" ,m,  mp->entries[m].data);

		WebConfigLog("mp->entries[%d].data_size is %zu\n", m,mp->entries[m].data_size);

		WebConfigLog("--------------decode root doc-------------\n");
		pm = webcfgparam_convert( mp->entries[m].data, mp->entries[m].data_size+1 );
		WebConfigLog("After webcfgparam_convert\n");
		if ( NULL != pm)
		{
			paramCount = (int)pm->entries_count;
			for(i = 0; i < paramCount ; i++)
			{
				printf("pm->entries[%d].name %s\n", i, pm->entries[i].name);
				printf("pm->entries[%d].value %s\n" , i, pm->entries[i].value);
				printf("pm->entries[%d].type %d\n", i, pm->entries[i].type);
				WebConfigLog("--------------decode root doc done-------------\n");
			}

			reqParam = (param_t *) malloc(sizeof(param_t) * paramCount);
			memset(reqParam,0,(sizeof(param_t) * paramCount));

			WebConfigLog("paramCount is %d\n", paramCount);

			for (i = 0; i < paramCount; i++) 
			{
				reqParam[i].name = strdup(pm->entries[i].name);
				reqParam[i].value = strdup(pm->entries[i].value);
				reqParam[i].type = pm->entries[i].type;

				WebConfigLog("--->Request:> param[%d].name = %s\n",i,reqParam[i].name);
				WebConfigLog("--->Request:> param[%d].value = %s\n",i,reqParam[i].value);
				WebConfigLog("--->Request:> param[%d].type = %d\n",i,reqParam[i].type);
			}

			WebConfigLog("Proceed to setValues\n");
			if(reqParam !=NULL)
			{
				WebcfgInfo("WebConfig SET Request\n");

				setValues(reqParam, paramCount, 0, NULL, NULL, &ret, &ccspStatus);
				WebcfgInfo("Processed WebConfig SET Request\n");
				WebcfgInfo("ccspStatus is %d\n", ccspStatus);
				if(ret == WDMP_SUCCESS)
				{
				        WebConfigLog("setValues success. ccspStatus : %d\n", ccspStatus);
					rv = 0;
				}
				else
				{
				      WebConfigLog("setValues Failed. ccspStatus : %d\n", ccspStatus);
				}
				//WEBCFG_FREE(reqParam);
			}
			webcfgparam_destroy( pm );
		}
		else
		{
			WebConfigLog("--------------decode root doc failed-------------\n");	
		}
	}
	return rv;
}

/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/

/* @brief callback function for writing libcurl received data
 * @param[in] buffer curl delivered data which need to be saved.
 * @param[in] size size is always 1
 * @param[in] nmemb size of delivered data
 * @param[out] data curl response data saved.
*/
size_t writer_callback_fn(void *buffer, size_t size, size_t nmemb, struct token_data *data)
{
    size_t index = data->size;
    size_t n = (size * nmemb);
    char* tmp; 
    data->size += (size * nmemb);

    tmp = realloc(data->data, data->size + 1); // +1 for '\0' 

    if(tmp) {
        data->data = tmp;
    } else {
        if(data->data) {
            free(data->data);
        }
        WebConfigLog("Failed to allocate memory for data\n");
        return 0;
    }
    memcpy((data->data + index), buffer, n);
    data->data[data->size] = '\0';
    WebConfigLog("size * nmemb is %zu\n", size * nmemb);
    return size * nmemb;
}

/* @brief callback function to extract response header data.
*/
size_t headr_callback(char *buffer, size_t size, size_t nitems)
{
	size_t etag_len = 0;
	char* header_value = NULL;
	char* final_header = NULL;
	char header_str[64] = {'\0'};

	etag_len = strlen(ETAG_HEADER);
	if( nitems > etag_len )
	{
		if( strncasecmp(ETAG_HEADER, buffer, etag_len) == 0 )
		{
			header_value = strtok(buffer, ":");
			while( header_value != NULL )
			{
				header_value = strtok(NULL, ":");
				if(header_value !=NULL)
				{
					strncpy(header_str, header_value, sizeof(header_str)-1);
					stripspaces(header_str, &final_header);

					strncpy(g_ETAG, final_header, sizeof(g_ETAG)-1);
				}
			}
		}
	}
	WebConfigLog("header_callback size %zu\n", size);
	return nitems;
}

//To strip all spaces , new line & carriage return characters from header output
void stripspaces(char *str, char **final_str)
{
	int i=0, j=0;

	for(i=0;str[i]!='\0';++i)
	{
		if(str[i]!=' ')
		{
			if(str[i]!='\n')
			{
				if(str[i]!='\r')
				{
					str[j++]=str[i];
				}
			}
		}
	}
	str[j]='\0';
	*final_str = str;
}

int readFromFile(char *filename, char **data, int *len)
{
	FILE *fp;
	int ch_count = 0;
	fp = fopen(filename, "r+");
	if (fp == NULL)
	{
		WebConfigLog("Failed to open file %s\n", filename);
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	ch_count = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	*data = (char *) malloc(sizeof(char) * (ch_count + 1));
	fread(*data, 1, ch_count-1,fp);
	*len = ch_count;
	(*data)[ch_count] ='\0';
	fclose(fp);
	return 1;
}

/* @brief Function to create curl header options
 * @param[in] list temp curl header list
 * @param[in] device status value
 * @param[out] header_list output curl header list
*/
void createCurlHeader( struct curl_slist *list, struct curl_slist **header_list, int status, char* doc, char ** trans_uuid)
{
	char *version_header = NULL;
	char *auth_header = NULL;
	char *status_header=NULL;
	char *schema_header=NULL;
	char *bootTime = NULL, *bootTime_header = NULL;
	char *FwVersion = NULL, *FwVersion_header=NULL;
        char *productClass = NULL, *productClass_header = NULL;
	char *ModelName = NULL, *ModelName_header = NULL;
	char *systemReadyTime = NULL, *systemReadyTime_header=NULL;
	struct timespec cTime;
	char currentTime[32];
	char *currentTime_header=NULL;
	char *uuid_header = NULL;
	char *transaction_uuid = NULL;
	char *version = NULL;
	char* syncTransID = NULL;
	char* ForceSyncDoc = NULL;

	WebConfigLog("Start of createCurlheader\n");
	//Fetch auth JWT token from cloud.
	getAuthToken();
	//int len=0; char *token= NULL;
	WebConfigLog("get_global_auth_token() is %s\n", get_global_auth_token());
	//readFromFile("/tmp/webcfg_token", &token, &len );
	//strncpy(get_global_auth_token(), token, len);

	auth_header = (char *) malloc(sizeof(char)*MAX_HEADER_LEN);
	if(auth_header !=NULL)
	{
		snprintf(auth_header, MAX_HEADER_LEN, "Authorization:Bearer %s", (0 < strlen(get_global_auth_token()) ? get_global_auth_token() : NULL));
		list = curl_slist_append(list, auth_header);
		WEBCFG_FREE(auth_header);
	}
	version_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
	if(version_header !=NULL)
	{
		printf("doc is %s\n", doc);
		//_getConfigVersion(index, &version); :TODO
		snprintf(version_header, MAX_BUF_SIZE, "IF-NONE-MATCH:%s", ((NULL != version && (strlen(version)!=0)) ? version : "NONE"));
		WebConfigLog("version_header formed %s\n", version_header);
		list = curl_slist_append(list, version_header);
		WEBCFG_FREE(version_header);
		if(version !=NULL)
		{
			WEBCFG_FREE(version);
		}
	}
	list = curl_slist_append(list, "Accept: application/msgpack");

	schema_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
	if(schema_header !=NULL)
	{
		snprintf(schema_header, MAX_BUF_SIZE, "Schema-Version: %s", "v1.0");
		WebConfigLog("schema_header formed %s\n", schema_header);
		list = curl_slist_append(list, schema_header);
		WEBCFG_FREE(schema_header);
	}

	if(strlen(g_bootTime) ==0)
	{
		bootTime = getDeviceBootTime();
		if(bootTime !=NULL)
		{
		       strncpy(g_bootTime, bootTime, sizeof(g_bootTime)-1);
		       WebcfgDebug("g_bootTime fetched is %s\n", g_bootTime);
		       WEBCFG_FREE(bootTime);
		}
	}

	if(strlen(g_bootTime))
	{
		bootTime_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
		if(bootTime_header !=NULL)
		{
			snprintf(bootTime_header, MAX_BUF_SIZE, "X-System-Boot-Time: %s", g_bootTime);
			WebConfigLog("bootTime_header formed %s\n", bootTime_header);
			list = curl_slist_append(list, bootTime_header);
			WEBCFG_FREE(bootTime_header);
		}
	}
	else
	{
		WebConfigLog("Failed to get bootTime\n");
	}

	if(strlen(g_FirmwareVersion) ==0)
	{
		FwVersion = getFirmwareVersion();
		if(FwVersion !=NULL)
		{
		       strncpy(g_FirmwareVersion, FwVersion, sizeof(g_FirmwareVersion)-1);
		       WebcfgDebug("g_FirmwareVersion fetched is %s\n", g_FirmwareVersion);
		       WEBCFG_FREE(FwVersion);
		}
	}

	if(strlen(g_FirmwareVersion))
	{
		FwVersion_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
		if(FwVersion_header !=NULL)
		{
			snprintf(FwVersion_header, MAX_BUF_SIZE, "X-System-Firmware-Version: %s", g_FirmwareVersion);
			WebConfigLog("FwVersion_header formed %s\n", FwVersion_header);
			list = curl_slist_append(list, FwVersion_header);
			WEBCFG_FREE(FwVersion_header);
		}
	}
	else
	{
		WebConfigLog("Failed to get FwVersion\n");
	}

	status_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
	if(status_header !=NULL)
	{
		if(status !=0)
		{
			snprintf(status_header, MAX_BUF_SIZE, "X-System-Status: %s", "Non-Operational");
		}
		else
		{
			snprintf(status_header, MAX_BUF_SIZE, "X-System-Status: %s", "Operational");
		}
		WebConfigLog("status_header formed %s\n", status_header);
		list = curl_slist_append(list, status_header);
		WEBCFG_FREE(status_header);
	}

	memset(currentTime, 0, sizeof(currentTime));
	getCurrent_Time(&cTime);
	snprintf(currentTime,sizeof(currentTime),"%d",(int)cTime.tv_sec);
	currentTime_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
	if(currentTime_header !=NULL)
	{
		snprintf(currentTime_header, MAX_BUF_SIZE, "X-System-Current-Time: %s", currentTime);
		WebConfigLog("currentTime_header formed %s\n", currentTime_header);
		list = curl_slist_append(list, currentTime_header);
		WEBCFG_FREE(currentTime_header);
	}

        if(strlen(g_systemReadyTime) ==0)
        {
                systemReadyTime = get_global_systemReadyTime();
                if(systemReadyTime !=NULL)
                {
                       strncpy(g_systemReadyTime, systemReadyTime, sizeof(g_systemReadyTime)-1);
                       WebcfgDebug("g_systemReadyTime fetched is %s\n", g_systemReadyTime);
                       WEBCFG_FREE(systemReadyTime);
                }
        }

        if(strlen(g_systemReadyTime))
        {
                systemReadyTime_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
                if(systemReadyTime_header !=NULL)
                {
	                snprintf(systemReadyTime_header, MAX_BUF_SIZE, "X-System-Ready-Time: %s", g_systemReadyTime);
	                WebConfigLog("systemReadyTime_header formed %s\n", systemReadyTime_header);
	                list = curl_slist_append(list, systemReadyTime_header);
	                WEBCFG_FREE(systemReadyTime_header);
                }
        }
        else
        {
                WebConfigLog("Failed to get systemReadyTime\n");
        }

	WebConfigLog("B4 getForceSync\n");
	getForceSync(&ForceSyncDoc, &syncTransID);
	WebConfigLog("ForceSyncDoc %s syncTransID %s\n", ForceSyncDoc, syncTransID);
	if(syncTransID !=NULL)
	{
		if(ForceSyncDoc && strlen(syncTransID)>0)
		{
			WebConfigLog("updating transaction_uuid with force syncTransID\n");
			transaction_uuid = strdup(syncTransID);
		}
		WEBCFG_FREE(syncTransID);
	}
	WebConfigLog("transaction_uuid is %s\n", transaction_uuid);

	if(transaction_uuid == NULL)
	{
		transaction_uuid = generate_trans_uuid();
	}

	if(transaction_uuid !=NULL)
	{
		uuid_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
		if(uuid_header !=NULL)
		{
			snprintf(uuid_header, MAX_BUF_SIZE, "Transaction-ID: %s", transaction_uuid);
			WebConfigLog("uuid_header formed %s\n", uuid_header);
			list = curl_slist_append(list, uuid_header);
			*trans_uuid = strdup(transaction_uuid);
			WEBCFG_FREE(transaction_uuid);
			WEBCFG_FREE(uuid_header);
		}
	}
	else
	{
		WebConfigLog("Failed to generate transaction_uuid\n");
	}

	if(strlen(g_productClass) ==0)
	{
		productClass = getProductClass();
		if(productClass !=NULL)
		{
		       strncpy(g_productClass, productClass, sizeof(g_productClass)-1);
		       WebcfgDebug("g_productClass fetched is %s\n", g_productClass);
		       WEBCFG_FREE(productClass);
		}
	}

	if(strlen(g_productClass))
	{
		productClass_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
		if(productClass_header !=NULL)
		{
			snprintf(productClass_header, MAX_BUF_SIZE, "X-System-Product-Class: %s", g_productClass);
			WebConfigLog("productClass_header formed %s\n", productClass_header);
			list = curl_slist_append(list, productClass_header);
			WEBCFG_FREE(productClass_header);
		}
	}
	else
	{
		WebConfigLog("Failed to get productClass\n");
	}

	if(strlen(g_ModelName) ==0)
	{
		ModelName = getModelName();
		if(ModelName !=NULL)
		{
		       strncpy(g_ModelName, ModelName, sizeof(g_ModelName)-1);
		       WebcfgDebug("g_ModelName fetched is %s\n", g_ModelName);
		       WEBCFG_FREE(ModelName);
		}
	}

	if(strlen(g_ModelName))
	{
		ModelName_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
		if(ModelName_header !=NULL)
		{
			snprintf(ModelName_header, MAX_BUF_SIZE, "X-System-Model-Name: %s", g_ModelName);
			WebConfigLog("ModelName_header formed %s\n", ModelName_header);
			list = curl_slist_append(list, ModelName_header);
			WEBCFG_FREE(ModelName_header);
		}
	}
	else
	{
		WebConfigLog("Failed to get ModelName\n");
	}
	*header_list = list;
}

char* generate_trans_uuid()
{
	char *transID = NULL;
	uuid_t transaction_Id;
	char *trans_id = NULL;
	trans_id = (char *)malloc(37);
	uuid_generate_random(transaction_Id);
	uuid_unparse(transaction_Id, trans_id);

	if(trans_id !=NULL)
	{
		transID = trans_id;
	}
	return transID;
}

void multipart_destroy( multipart_t *m )
{
    if( NULL != m ) {
     /*   size_t i;
        for( i = 0; i < m->entries_count; i++ ) {
            if( NULL != m->entries[i].name_space ) {
                printf("name_space %ld",i);
                free( m->entries[i].name_space );
            }
	    if( NULL != m->entries[i].etag ) {
                printf("etag %ld",i);
                free( m->entries[i].etag );
            }
             if( NULL != m->entries[i].data ) {
                printf("data %ld",i);
                free( m->entries[i].data );
            }
        }
        if( NULL != m->entries ) {
            printf("entries %ld",i);
            free( m->entries );
        }*/
        free( m );
    }
}


int writeToFile(char *filename, char *data, int len)
{
	FILE *fp;
	fp = fopen(filename , "w+");
	if (fp == NULL)
	{
		WebConfigLog("Failed to open file %s\n", filename );
		return 0;
	}
	if(data !=NULL)
	{
		fwrite(data, len, 1, fp);
		fclose(fp);
		return 1;
	}
	else
	{
		WebConfigLog("WriteToFile failed, Data is NULL\n");
		return 0;
	}
}

void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m, int *no_of_subdocbytes)
{
	void * mulsubdoc;

	/*for storing respective values */
	if(0 == strncasecmp(ptr,"Namespace",strlen("Namespace")-1))
	{
		m->name_space = strndup(ptr,no_of_bytes);
		WebConfigLog("The Namespace is %s\n",m->name_space);
	}
	else if(0 == strncasecmp(ptr,"Etag",strlen("Etag")-1))
	{
		m->etag = strndup(ptr,no_of_bytes);
		WebConfigLog("The Etag is %s\n",m->etag);
	}
	else if(strstr(ptr,"parameters"))
	{
		m->data = ptr;
		mulsubdoc = (void *) ptr;
		WebConfigLog("The paramters is %s\n",m->data);
		WebConfigLog("no_of_bytes is %d\n", no_of_bytes);
		webcfgparam_convert( mulsubdoc, no_of_bytes );
		*no_of_subdocbytes = no_of_bytes;
		WebConfigLog("*no_of_subdocbytes is %d\n", *no_of_subdocbytes);
		//store doc size of each sub doc
		m->data_size = no_of_bytes;
	}
}

int subdocparse(char *filename, char **data, int *len)
{
	FILE *fp = fopen(filename, "rb");
	int ch_count = 0, value_count = 0;
	char line[256];
	if (fp == NULL)
	{
		WebConfigLog("subdocparse Failed to open file %s\n", filename);
		return 0;
	}
	else
	{
		fseek(fp, 0, SEEK_END);
		ch_count = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		*data = (char *) malloc(sizeof(char) * (ch_count + 1));
		while(fgets(line, sizeof(line), fp)!= NULL)
		{
			if(strstr(line, ETAG_HEADER))
			{
				value_count = ftell(fp) + 2; // 2 is for newline \n \r
				fseek(fp, value_count, SEEK_SET);
				fread(*data,1, ch_count,fp);

				*len = ch_count;
				(*data)[ch_count] ='\0';
				fclose(fp);
				WebConfigLog("ch_count %d\n",ch_count);
				writeToFile("buff1.bin", (char*)*data,(ch_count-value_count));
				return 1;

			}
		}
		fclose(fp);
		return 0;
	  }
}
