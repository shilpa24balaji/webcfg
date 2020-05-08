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
#include "webcfg_multipart.h"
#include "webcfg_param.h"
#include "webcfg_log.h"
#include "webcfg_auth.h"
#include "webcfg_generic.h"
#include "webcfg_db.h"
#include "webcfg_notify.h"
#include <uuid/uuid.h>
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
#define MAX_HEADER_LEN			4096
#define ETAG_HEADER 		       "Etag:"
#define CURL_TIMEOUT_SEC	   25L
#define CA_CERT_PATH 		   "/etc/ssl/certs/ca-certificates.crt"
#define WEBPA_READ_HEADER          "/etc/parodus/parodus_read_file.sh"
#define WEBPA_CREATE_HEADER        "/etc/parodus/parodus_create_file.sh"
#define CCSP_CRASH_STATUS_CODE      192
#define ATOMIC_SET_WEBCONFIG	    3
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
void createCurlHeader( struct curl_slist *list, struct curl_slist **header_list, int status, char ** trans_uuid);
void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m);
void multipart_destroy( multipart_t *m );
void addToDBList(webconfig_db_data_t *webcfgdb);
char* generate_trans_uuid();
WEBCFG_STATUS processMsgpackSubdoc(multipart_t *mp, char *transaction_id);
void loadInitURLFromFile(char **url);
static void get_webCfg_interface(char **interface);
void get_root_version(uint32_t *rt_version);
char *replaceMacWord(const char *s, const char *macW, const char *deviceMACW);
void reqParam_destroy( int paramCnt, param_t *reqObj );
/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/
/*
* @brief Initialize curl object with required options. create configData using libcurl.
* @param[out] configData
* @param[in] r_count Number of curl retries on ipv4 and ipv6 mode during failure
* @param[in] doc name to detect force sync
* @param[in] status device operational status
* @param[out] code curl response code
* @param[out] transaction_id use webpa transaction_id for webpa force sync, else generate new id.
* @param[out] contentType config data contentType 
* @return returns 0 if success, otherwise failed to fetch auth token and will be retried.
*/
WEBCFG_STATUS webcfg_http_request(char **configData, int r_count, int status, long *code, char **transaction_id, char* contentType, size_t *dataSize)
{
	CURL *curl;
	CURLcode res;
	CURLcode time_res;
	struct curl_slist *list = NULL;
	struct curl_slist *headers_list = NULL;
	double total;
	long response_code = 0;
	char *interface = NULL;
	char *ct = NULL;
	char *webConfigURL = NULL;
	char *transID = NULL;
	char docList[512] = {'\0'};
	char configURL[256] = { 0 };
	char c[] = "{mac}";
	int rv = 0;

	int content_res=0;
	struct token_data data;
	data.size = 0;
	void * dataVal = NULL;
	char syncURL[256]={'\0'};

	curl = curl_easy_init();
	if(curl)
	{
		//this memory will be dynamically grown by write call back fn as required
		data.data = (char *) malloc(sizeof(char) * 1);
		if(NULL == data.data)
		{
			WebcfgError("Failed to allocate memory.\n");
			return WEBCFG_FAILURE;
		}
		data.data[0] = '\0';
		createCurlHeader(list, &headers_list, status, &transID);
		if(transID !=NULL)
		{
			*transaction_id = strdup(transID);
			WEBCFG_FREE(transID);
		}
		//loadInitURLFromFile(&webConfigURL);
		Get_Webconfig_URL(configURL);
		if(strlen(configURL)>0)
		{
			//Replace {mac} string from default init url with actual deviceMAC
			WebcfgDebug("replaceMacWord to actual device mac\n");
			webConfigURL = replaceMacWord(configURL, c, get_deviceMAC());
			Set_Webconfig_URL(webConfigURL);
		}
		else
		{
			WebcfgError("Failed to get configURL\n");
			WEBCFG_FREE(data.data);
			curl_slist_free_all(headers_list);
			curl_easy_cleanup(curl);
			return WEBCFG_FAILURE;
		}
		WebcfgDebug("ConfigURL fetched is %s\n", webConfigURL);

		//Update query param in the URL based on the existing doc names from db
		getConfigDocList(docList);
		if(strlen(docList) > 0)
		{
			WebcfgInfo("docList is %s\n", docList);
			snprintf(syncURL, MAX_BUF_SIZE, "%s?group_id=%s", webConfigURL, docList);
			WEBCFG_FREE(webConfigURL);
			WebcfgDebug("syncURL is %s\n", syncURL);
			webConfigURL =strdup( syncURL);
		}

		if(webConfigURL !=NULL)
		{
			WebcfgInfo("Webconfig root ConfigURL is %s\n", webConfigURL);
			res = curl_easy_setopt(curl, CURLOPT_URL, webConfigURL );
		}
		else
		{
			WebcfgError("Failed to get webconfig configURL\n");
			WEBCFG_FREE(data.data);
			curl_slist_free_all(headers_list);
			curl_easy_cleanup(curl);
			return WEBCFG_FAILURE;
		}
		res = curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SEC);
		WebcfgDebug("fetching interface from device.properties\n");
		if(strlen(g_interface) == 0)
		{
			get_webCfg_interface(&interface);
			if(interface !=NULL)
		        {
		               strncpy(g_interface, interface, sizeof(g_interface)-1);
		               WebcfgDebug("g_interface copied is %s\n", g_interface);
		               WEBCFG_FREE(interface);
		        }
		}
		WebcfgInfo("g_interface fetched is %s\n", g_interface);
		if(strlen(g_interface) > 0)
		{
			WebcfgDebug("setting interface %s\n", g_interface);
			res = curl_easy_setopt(curl, CURLOPT_INTERFACE, g_interface);
		}

		// set callback for writing received data
		dataVal = &data;
		res = curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writer_callback_fn);
		res = curl_easy_setopt(curl, CURLOPT_WRITEDATA, dataVal);

		res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers_list);

		res = curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, headr_callback);

		// setting curl resolve option as default mode.
		//If any failure, retry with v4 first and then v6 mode.
		if(r_count == 1)
		{
			WebcfgInfo("curl Ip resolve option set as V4 mode\n");
			res = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
		}
		else if(r_count == 2)
		{
			WebcfgInfo("curl Ip resolve option set as V6 mode\n");
			res = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V6);
		}
		else
		{
			WebcfgInfo("curl Ip resolve option set as default mode\n");
			res = curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_WHATEVER);
		}
		res = curl_easy_setopt(curl, CURLOPT_CAINFO, CA_CERT_PATH);
		// disconnect if it is failed to validate server's cert
		res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
		// Verify the certificate's name against host
  		res = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
		// To use TLS version 1.2 or later
  		res = curl_easy_setopt(curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
		// To follow HTTP 3xx redirections
  		res = curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
		// Perform the request, res will get the return code
		res = curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
		WebcfgInfo("webConfig curl response %d http_code %ld\n", res, response_code);
		*code = response_code;
		time_res = curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total);
		if(time_res == 0)
		{
			WebcfgInfo("curl response Time: %.1f seconds\n", total);
		}
		curl_slist_free_all(headers_list);
		WEBCFG_FREE(webConfigURL);
		if(res != 0)
		{
			WebcfgError("curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
		}
		else
		{
			if(response_code == 200)
                        {
				WebcfgDebug("checking content type\n");
				content_res = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct);
				WebcfgDebug("ct is %s, content_res is %d\n", ct, content_res);

				if(ct !=NULL)
				{
					if(strncmp(ct, "multipart/mixed", 15) !=0)
					{
						WebcfgError("Content-Type is not multipart/mixed. Invalid\n");
					}
					else
					{
						WebcfgInfo("Content-Type is multipart/mixed. Valid\n");
						strcpy(contentType, ct);

						*configData=data.data;
						*dataSize = data.size;
						WebcfgDebug("Data size is %d\n",(int)data.size);
						rv = 1;
					}
				}
			}
		}
		if(rv != 1)
		{
			WEBCFG_FREE(data.data);
		}
		curl_easy_cleanup(curl);
		return WEBCFG_SUCCESS;
	}
	else
	{
		WebcfgError("curl init failure\n");
	}
	return WEBCFG_FAILURE;
}

WEBCFG_STATUS parseMultipartDocument(void *config_data, char *ct , size_t data_size, char* trans_uuid)
{
	char *boundary = NULL;
	char *str=NULL;
	char *line_boundary = NULL;
	char *last_line_boundary = NULL;
	char *str_body = NULL;
	multipart_t *mp = NULL;
	int boundary_len =0;
	int count =0;
	int status =0;
	
	WebcfgInfo("ct is %s\n", ct );
	// fetch boundary
	str = strtok(ct,";");
	str = strtok(NULL, ";");
	boundary= strtok(str,"=");
	boundary= strtok(NULL,"=");
	WebcfgDebug( "boundary %s\n", boundary );
	
	if(boundary !=NULL)
	{
		boundary_len= strlen(boundary);
		line_boundary  = (char *)malloc(sizeof(char) * (boundary_len +5));
		snprintf(line_boundary,boundary_len+5,"--%s\r\n",boundary);
		WebcfgDebug( "line_boundary %s, len %zu\n", line_boundary, strlen(line_boundary) );

		last_line_boundary  = (char *)malloc(sizeof(char) * (boundary_len + 5));
		snprintf(last_line_boundary,boundary_len+5,"--%s--",boundary);
		WebcfgDebug( "last_line_boundary %s, len %zu\n", last_line_boundary, strlen(last_line_boundary) );
		// Use --boundary to split
		str_body = malloc(sizeof(char) * data_size + 1);
		str_body = memcpy(str_body, config_data, data_size + 1);
		WEBCFG_FREE(config_data);
		int num_of_parts = 0;
		char *ptr_lb=str_body;
		char *ptr_lb1=str_body;
		char *ptr_count = str_body;
		int index1=0, index2 =0;

		/* For Subdocs count */
		while((ptr_count - str_body) < (int)data_size )
		{
			ptr_count = memchr(ptr_count, '-', data_size - (ptr_count - str_body));
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
		WebcfgInfo("Size of the docs is :%d\n", (num_of_parts-1));
		/* For Subdocs count */

		mp = (multipart_t *) malloc (sizeof(multipart_t));
		mp->entries_count = (size_t)num_of_parts;
		mp->entries = (multipartdocs_t *) malloc(sizeof(multipartdocs_t )*(mp->entries_count-1) );
		memset( mp->entries, 0, sizeof(multipartdocs_t)*(mp->entries_count-1));
		///Scanning each lines with \n as delimiter
		while((ptr_lb - str_body) < (int)data_size)
		{
			ptr_lb = memchr(ptr_lb, '-', data_size - (ptr_lb - str_body));
			if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
			{
				WebcfgDebug("last line boundary \n");
				break;
			}
			if(0 == memcmp(ptr_lb, line_boundary, strlen(line_boundary)))
			{
				ptr_lb = ptr_lb+(strlen(line_boundary))-1;
				num_of_parts = 1;
				while(0 != num_of_parts % 2)
				{
					ptr_lb1 = memchr(ptr_lb+1, '\n', data_size - (ptr_lb - str_body));
					if(0 != memcmp(ptr_lb1-1, "\r",1 )){
					ptr_lb1 = memchr(ptr_lb1+1, '\n', data_size - (ptr_lb - str_body));
					}
					index2 = ptr_lb1-str_body;
					index1 = ptr_lb-str_body;
					parse_multipart(str_body+index1+1,index2 - index1 - 2, &mp->entries[count]);
					ptr_lb++;
					if(0 == memcmp(ptr_lb, last_line_boundary, strlen(last_line_boundary)))
					{
						WebcfgDebug("last line boundary inside \n");
						break;
					}
					if(0 == memcmp(ptr_lb1+1, "-", 1) && 0 == memcmp(ptr_lb1+1, line_boundary, strlen(line_boundary)))
					{
						WebcfgDebug(" line boundary inside \n");
						num_of_parts++;
						count++;
					}
					ptr_lb = memchr(ptr_lb, '\n', data_size - (ptr_lb - str_body));
}
			}
			else
			{
				ptr_lb++;
			}
		}
		WEBCFG_FREE(str_body);
		WEBCFG_FREE(line_boundary);
		WEBCFG_FREE(last_line_boundary);
		status = processMsgpackSubdoc(mp, trans_uuid);
		if(status ==0)
		{
			WebcfgInfo("processMsgpackSubdoc success\n");
			return WEBCFG_SUCCESS;
		}
		else
		{
			WebcfgError("processMsgpackSubdoc failed,as all the docs are not applied\n");
		}
		return WEBCFG_FAILURE;
	}
    else
    {
		WebcfgError("Multipart Boundary is NULL\n");
		return WEBCFG_FAILURE;
    }
}

WEBCFG_STATUS processMsgpackSubdoc(multipart_t *mp, char *transaction_id)
{
	int i =0, m=0;
	WEBCFG_STATUS rv = WEBCFG_FAILURE;
	param_t *reqParam = NULL;
	WDMP_STATUS ret = WDMP_FAILURE;
	int ccspStatus=0;
	int paramCount = 0;
	webcfgparam_t *pm;
	int success_count = 0;
	WEBCFG_STATUS addStatus =0;
	WEBCFG_STATUS uStatus = 0, dStatus =0;
	char * blob_data = NULL;
        size_t blob_len = -1 ;
	char * trans_id = NULL;

	if(transaction_id !=NULL)
	{
		trans_id = strdup(transaction_id);
		WEBCFG_FREE(transaction_id);
	}
        
	WebcfgDebug("Add mp entries to tmp list\n");
        addStatus = addToTmpList(mp);
        if(addStatus == WEBCFG_SUCCESS)
        {
		WebcfgInfo("Added %d mp entries To tmp List\n", get_numOfMpDocs());
		print_tmp_doc_list(mp->entries_count);
        }
	else
	{
		WebcfgError("addToTmpList failed\n");
	}

	for(m = 0 ; m<((int)mp->entries_count)-1; m++)
	{       
		WebcfgInfo("mp->entries[%d].name_space %s\n", m, mp->entries[m].name_space);
		WebcfgInfo("mp->entries[%d].etag %lu\n" ,m,  (long)mp->entries[m].etag);
		WebcfgDebug("mp->entries[%d].data %s\n" ,m,  mp->entries[m].data);

		WebcfgDebug("mp->entries[%d].data_size is %zu\n", m,mp->entries[m].data_size);

		WebcfgDebug("--------------decode root doc-------------\n");
		pm = webcfgparam_convert( mp->entries[m].data, mp->entries[m].data_size+1 );
		if ( NULL != pm)
		{
			paramCount = (int)pm->entries_count;

			reqParam = (param_t *) malloc(sizeof(param_t) * paramCount);
			memset(reqParam,0,(sizeof(param_t) * paramCount));

			WebcfgDebug("paramCount is %d\n", paramCount);
			
			for (i = 0; i < paramCount; i++) 
			{
                                if(pm->entries[i].value != NULL)
                                {
					if(pm->entries[i].type == WDMP_BLOB)
					{
						char *temp_blob = NULL;

						temp_blob = base64blobencoder(pm->entries[i].value, pm->entries[i].value_size);
						reqParam[i].name = strdup(pm->entries[i].name);
						reqParam[i].value = strdup(temp_blob);
						WEBCFG_FREE(temp_blob);
						reqParam[i].type = WDMP_BASE64;
					}
					else
					{
						reqParam[i].name = strdup(pm->entries[i].name);
						reqParam[i].value = strdup(pm->entries[i].value);
						reqParam[i].type = pm->entries[i].type;
					}
                                }
				WebcfgInfo("Request:> param[%d].name = %s, type = %d\n",i,reqParam[i].name,reqParam[i].type);
				WebcfgDebug("Request:> param[%d].value = %s\n",i,reqParam[i].value);
				WebcfgDebug("Request:> param[%d].type = %d\n",i,reqParam[i].type);
			}

			WebcfgDebug("Proceed to setValues..\n");
			if(reqParam !=NULL)
			{
				WebcfgInfo("WebConfig SET Request\n");
				setValues(reqParam, paramCount, ATOMIC_SET_WEBCONFIG, NULL, NULL, &ret, &ccspStatus);
				if(ret == WDMP_SUCCESS)
				{
					WebcfgInfo("setValues success. ccspStatus : %d\n", ccspStatus);

					WebcfgDebug("update doc status for %s\n", mp->entries[m].name_space);
					uStatus = updateTmpList(mp->entries[m].name_space, mp->entries[m].etag, "success", "none");
					if(uStatus == WEBCFG_SUCCESS)
					{
						//print_tmp_doc_list(mp->entries_count);
						WebcfgDebug("updateTmpList success\n");

						//send success notification to cloud
						WebcfgDebug("send notify for mp->entries[m].name_space %s\n", mp->entries[m].name_space);
						addWebConfgNotifyMsg(mp->entries[m].name_space, mp->entries[m].etag, "success", "none", trans_id);
						WebcfgDebug("deleteFromTmpList as doc is applied\n");
						dStatus = deleteFromTmpList(mp->entries[m].name_space);
						if(dStatus == WEBCFG_SUCCESS)
						{
							WebcfgDebug("deleteFromTmpList success\n");
							//print_tmp_doc_list(mp->entries_count);
						}
						else
						{
							WebcfgError("deleteFromTmpList failed\n");
						}
					}
					else
					{
						WebcfgError("updateTmpList failed\n");
					}
					checkDBList(mp->entries[m].name_space,mp->entries[m].etag);

					success_count++;

					WebcfgDebug("The mp->entries_count %d\n",(int)mp->entries_count);
					WebcfgDebug("The count %d\n",success_count);
					if(success_count ==(int) mp->entries_count-1)
					{
						char * temp = strdup(g_ETAG);
						uint32_t version=0;
						if(temp)
						{
							version = strtoul(temp,NULL,0);
							WEBCFG_FREE(temp);
						}
						if(version != 0)
						{
							checkDBList("root",version);
                        				success_count++;
						}

						WebcfgInfo("The Etag is %lu\n",(long)version );
						//Delete tmp queue root as all docs are applied
						WebcfgInfo("Delete tmp queue root as all docs are applied\n");
						WebcfgDebug("root version to delete is %lu\n", (long)version);
						dStatus = deleteFromTmpList("root");
						if(dStatus == 0)
						{
							WebcfgDebug("Delete tmp queue root is success\n");
						}
						else
						{
							WebcfgError("Delete tmp queue root is failed\n");
						}
						WebcfgDebug("processMsgpackSubdoc is success as all the docs are applied\n");
						rv = WEBCFG_SUCCESS;
					}
				}
				else
				{
					WebcfgInfo("setValues Failed. ccspStatus : %d\n", ccspStatus);

					//Update error_details to tmp list and send failure notification to cloud.
					uStatus = WEBCFG_FAILURE;
					if(ccspStatus == CCSP_CRASH_STATUS_CODE)
					{
						WebcfgInfo("ccspStatus is crash %d\n", CCSP_CRASH_STATUS_CODE);
						uStatus = updateTmpList(mp->entries[m].name_space, mp->entries[m].etag, "failed", "crash");
						addWebConfgNotifyMsg(mp->entries[m].name_space, mp->entries[m].etag, "failed", "crash", trans_id);
					}
					else
					{
						uStatus = updateTmpList(mp->entries[m].name_space, mp->entries[m].etag, "failed", "doc_rejected");
						addWebConfgNotifyMsg(mp->entries[m].name_space, mp->entries[m].etag, "failed", "doc_rejected", trans_id);
					}

					if(uStatus == WEBCFG_SUCCESS)
					{
						WebcfgDebug("updateTmpList success for error_details\n");
					}
					else
					{
						WebcfgError("updateTmpList failed for error_details\n");
					}
					print_tmp_doc_list(mp->entries_count);
				}
				if(NULL != reqParam)
				{
					reqParam_destroy(paramCount, reqParam);
				}
			}
			webcfgparam_destroy( pm );
		}
		else
		{
			WebcfgError("--------------decode root doc failed-------------\n");
		}
	}
	WEBCFG_FREE(trans_id);
	multipart_destroy(mp);
        
	if(success_count) //No DB update when all docs failed.
	{
		size_t j=success_count;
		
		webconfig_db_data_t* temp1 = NULL;
		temp1 = get_global_db_node();

		while(temp1 )
		{
			WebcfgInfo("DB wd->entries[%lu].name: %s, version: %lu\n", success_count-j, temp1->name, (long)temp1->version);
			j--;
			temp1 = temp1->next;
		}
		WebcfgDebug("addNewDocEntry\n");
		addNewDocEntry(get_successDocCount());
	}

	WebcfgDebug("Proceed to generateBlob\n");
	if(generateBlob() == WEBCFG_SUCCESS)
	{
		blob_data = get_DB_BLOB_base64(&blob_len);
		if(blob_data !=NULL)
		{
			WebcfgDebug("The b64 encoded blob is : %s\n",blob_data);
			WebcfgInfo("The b64 encoded blob_length is : %zu\n",strlen(blob_data));
			WEBCFG_FREE(blob_data);
		}
		else
		{
			WebcfgError("Failed in blob base64 encode\n");
		}
	}
	else
	{
		WebcfgError("Failed in Blob generation\n");
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
        WebcfgError("Failed to allocate memory for data\n");
        return 0;
    }
    memcpy((data->data + index), buffer, n);
    data->data[data->size] = '\0';
    WebcfgDebug("size * nmemb is %zu\n", size * nmemb);
    return size * nmemb;
}

/* @brief callback function to extract response header data.
   This is to get multipart root version which is received as header.
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
	WebcfgDebug("header_callback size %zu\n", size);
	return nitems;
}

//Convert string root version from multipart header to uint32
uint32_t get_global_root()
{
	char * temp = NULL;
	uint32_t g_version = 0;

	temp = strdup(g_ETAG);
	if(temp)
	{
		g_version = strtoul(temp,NULL,0);
		WEBCFG_FREE(temp);
	}
	WebcfgDebug("g_version is %lu\n", (long)g_version);
	return g_version;
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

static void get_webCfg_interface(char **interface)
{

        FILE *fp = fopen(DEVICE_PROPS_FILE, "r");

        if (NULL != fp)
        {
                char str[255] = {'\0'};
                while (fgets(str, sizeof(str), fp) != NULL)
                {
                    char *value = NULL;

                    if(NULL != (value = strstr(str, "WEBCONFIG_INTERFACE=")))
                    {
                        value = value + strlen("WEBCONFIG_INTERFACE=");
                        value[strlen(value)-1] = '\0';
                        *interface = strdup(value);
                        break;
                    }

                }
                fclose(fp);
        }
        else
        {
                WebcfgError("Failed to open device.properties file:%s\n", DEVICE_PROPS_FILE);
        }

        if (NULL == *interface)
        {
                WebcfgError("WebConfig interface is not present in device.properties\n");

        }
        else
        {
                WebcfgDebug("interface fetched is %s\n", *interface);
        }
}

void loadInitURLFromFile(char **url)
{
	FILE *fp = fopen(DEVICE_PROPS_FILE, "r");

	if (NULL != fp)
	{
		char str[255] = {'\0'};
		while (fgets(str, sizeof(str), fp) != NULL)
		{
		    char *value = NULL;
                    if(NULL != (value = strstr(str, "WEBCONFIG_INIT_URL=")))
		    {
			value = value + strlen("WEBCONFIG_INIT_URL=");
			value[strlen(value)-1] = '\0';
			*url = strdup(value);
			break;
		    }

		}
		fclose(fp);
	}
	else
	{
		WebcfgError("Failed to open device.properties file:%s\n", DEVICE_PROPS_FILE);
	}
	if (NULL == *url)
	{
		WebcfgError("WebConfig url is not present in device.properties\n");
	}
	else
	{
		WebcfgDebug("url fetched is %s\n", *url);
	}
}

int readFromFile(char *filename, char **data, int *len)
{
	FILE *fp;
	size_t sz;
	int ch_count = 0;
	fp = fopen(filename, "r+");
	if (fp == NULL)
	{
		WebcfgError("Failed to open file %s\n", filename);
		return 0;
	}
	fseek(fp, 0, SEEK_END);
	ch_count = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	*data = (char *) malloc(sizeof(char) * (ch_count + 1));
	sz = fread(*data, 1, ch_count-1,fp);
	if (!sz) 
	{	
		fclose(fp);
		WebcfgError("fread failed.\n");
		WEBCFG_FREE(*data);
		return WEBCFG_FAILURE;
	}
	*len = ch_count;
	(*data)[ch_count] ='\0';
	fclose(fp);
	return 1;
}

/* Traverse through db list to get docnames of all docs with root.
e.g. root,ble,lan,mesh,moca. */
void getConfigDocList(char *docList)
{
	char *docList_tmp = NULL;
	webconfig_db_data_t *temp = NULL;
	temp = get_global_db_node();

	if( NULL != temp)
	{
		sprintf(docList, "%s", "root");
		WebcfgDebug("docList is %s\n", docList);

		while (NULL != temp)
		{
			if( temp->name != NULL)
			{
				if( strcmp(temp->name,"root") !=0 )
				{
					docList_tmp = strdup(docList);
					sprintf(docList, "%s,%s",docList_tmp, temp->name);
					WEBCFG_FREE(docList_tmp);
				}
			}
			temp= temp->next;
		}
		WebcfgDebug("Final docList is %s len %lu\n", docList, strlen(docList));
	}
}

void get_root_version(uint32_t *rt_version)
{
	webconfig_db_data_t *temp = NULL;
	temp = get_global_db_node();

	while (NULL != temp)
	{
		if( strcmp(temp->name, "root") == 0)
		{
			*rt_version = temp->version;
			WebcfgDebug("rt_version %lu\n", (long)*rt_version);
		}
		temp= temp->next;
	}
}

/* Traverse through db list to get versions of all docs with root.
e.g. IF-NONE-MATCH: 123,44317,66317,77317 where 123 is root version.
Currently versionsList length is fixed to 512 which can support up to 45 docs.
This can be increased if required. */
void getConfigVersionList(char *versionsList)
{
	char *versionsList_tmp = NULL;
	uint32_t root_version = 0;

	get_root_version(&root_version);
	WebcfgDebug("root_version %lu\n", (long)root_version);

	webconfig_db_data_t *temp = NULL;
	temp = get_global_db_node();

	if(NULL != temp)
	{
		if(root_version)
		{
			WebcfgDebug("Updating root_version to versionsList\n");
			sprintf(versionsList, "%lu", (long)root_version);
		}
		else
		{
			WebcfgDebug("Updating versionsList as NONE\n");
			sprintf(versionsList, "%s", "NONE");
		}
		WebcfgDebug("versionsList is %s\n", versionsList);

		while (NULL != temp)
		{
			if( temp->name != NULL)
			{
				if( strcmp(temp->name,"root") !=0 )
				{
					versionsList_tmp = strdup(versionsList);
					sprintf(versionsList, "%s,%lu",versionsList_tmp,(long)temp->version);
					WEBCFG_FREE(versionsList_tmp);
				}
			}
			temp= temp->next;
		}
		WebcfgDebug("Final versionsList is %s len %lu\n", versionsList, strlen(versionsList));
	}
}

/* @brief Function to create curl header options
 * @param[in] list temp curl header list
 * @param[in] device status value
 * @param[out] trans_uuid for sync
 * @param[out] header_list output curl header list
*/
void createCurlHeader( struct curl_slist *list, struct curl_slist **header_list, int status, char ** trans_uuid)
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
	char version[512]={'\0'};
	char* syncTransID = NULL;
	char* ForceSyncDoc = NULL;

	WebcfgInfo("Start of createCurlheader\n");
	//Fetch auth JWT token from cloud.
	getAuthToken();
	WebcfgDebug("get_global_auth_token() is %s\n", get_global_auth_token());

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
		getConfigVersionList(version);
		snprintf(version_header, MAX_BUF_SIZE, "IF-NONE-MATCH:%s", ((strlen(version)!=0) ? version : "NONE"));
		WebcfgInfo("version_header formed %s\n", version_header);
		list = curl_slist_append(list, version_header);
		WEBCFG_FREE(version_header);
	}
	list = curl_slist_append(list, "Accept: application/msgpack");

	schema_header = (char *) malloc(sizeof(char)*MAX_BUF_SIZE);
	if(schema_header !=NULL)
	{
		snprintf(schema_header, MAX_BUF_SIZE, "Schema-Version: %s", "v1.0");
		WebcfgInfo("schema_header formed %s\n", schema_header);
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
			WebcfgInfo("bootTime_header formed %s\n", bootTime_header);
			list = curl_slist_append(list, bootTime_header);
			WEBCFG_FREE(bootTime_header);
		}
	}
	else
	{
		WebcfgError("Failed to get bootTime\n");
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
			WebcfgInfo("FwVersion_header formed %s\n", FwVersion_header);
			list = curl_slist_append(list, FwVersion_header);
			WEBCFG_FREE(FwVersion_header);
		}
	}
	else
	{
		WebcfgError("Failed to get FwVersion\n");
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
		WebcfgInfo("status_header formed %s\n", status_header);
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
		WebcfgInfo("currentTime_header formed %s\n", currentTime_header);
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
	                WebcfgInfo("systemReadyTime_header formed %s\n", systemReadyTime_header);
	                list = curl_slist_append(list, systemReadyTime_header);
	                WEBCFG_FREE(systemReadyTime_header);
                }
        }
        else
        {
                WebcfgError("Failed to get systemReadyTime\n");
        }

	getForceSync(&ForceSyncDoc, &syncTransID);

	if(syncTransID !=NULL)
	{
		if(ForceSyncDoc !=NULL)
		{
			if (strlen(syncTransID)>0)
			{
				WebcfgInfo("updating transaction_uuid with force syncTransID\n");
				transaction_uuid = strdup(syncTransID);
			}
			WEBCFG_FREE(ForceSyncDoc);
		}
		WEBCFG_FREE(syncTransID);
	}

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
			WebcfgInfo("uuid_header formed %s\n", uuid_header);
			list = curl_slist_append(list, uuid_header);
			*trans_uuid = strdup(transaction_uuid);
			WEBCFG_FREE(transaction_uuid);
			WEBCFG_FREE(uuid_header);
		}
	}
	else
	{
		WebcfgError("Failed to generate transaction_uuid\n");
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
			WebcfgInfo("productClass_header formed %s\n", productClass_header);
			list = curl_slist_append(list, productClass_header);
			WEBCFG_FREE(productClass_header);
		}
	}
	else
	{
		WebcfgError("Failed to get productClass\n");
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
			WebcfgInfo("ModelName_header formed %s\n", ModelName_header);
			list = curl_slist_append(list, ModelName_header);
			WEBCFG_FREE(ModelName_header);
		}
	}
	else
	{
		WebcfgError("Failed to get ModelName\n");
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
        size_t i=0;
        for( i = 0; i < m->entries_count-1; i++ ) {
           if( NULL != m->entries[i].name_space ) {
                free( m->entries[i].name_space );
            }
             if( NULL != m->entries[i].data ) {
                free( m->entries[i].data );
            }
        }
        if( NULL != m->entries ) {
            free( m->entries );
        }
        free( m );
    }
}

void parse_multipart(char *ptr, int no_of_bytes, multipartdocs_t *m)
{
	char *Content_type = NULL;
	/*for storing respective values */
	if(0 == strncmp(ptr,"Content-type: ",strlen("Content-type")))
	{
		Content_type = strndup(ptr+(strlen("Content-type: ")),no_of_bytes-((strlen("Content-type: "))));
		if(strncmp(Content_type, "application/msgpack",strlen("application/msgpack")) !=0)
		{
			WebcfgError("Content-type not msgpack: %s", Content_type);
		}
		WEBCFG_FREE(Content_type);
	}
	else if(0 == strncasecmp(ptr,"Namespace",strlen("Namespace")))
	{
                m->name_space = strndup(ptr+(strlen("Namespace: ")),no_of_bytes-((strlen("Namespace: "))));
	}
	else if(0 == strncasecmp(ptr,"Etag",strlen("Etag")))
	{
                char * temp = strndup(ptr+(strlen("Etag: ")),no_of_bytes-((strlen("Etag: "))));
		if(temp)
		{
		        m->etag = strtoul(temp,0,0);
			WebcfgDebug("The Etag version is %lu\n",(long)m->etag);
			WEBCFG_FREE(temp);
		}
	}
	else if(strstr(ptr,"parameters"))
	{
		m->data = malloc(sizeof(char) * no_of_bytes );
		m->data = memcpy(m->data, ptr, no_of_bytes );
		//store doc size of each sub doc
		m->data_size = no_of_bytes;
	}
}

void print_tmp_doc_list(size_t mp_count)
{
	int count =0;
	webconfig_tmp_data_t *temp = NULL;
	temp = get_global_tmp_node();
	while (NULL != temp)
	{
		count = count+1;
		WebcfgInfo("node is pointing to temp->name %s temp->version %lu temp->status %s temp->error_details %s\n",temp->name, (long)temp->version, temp->status, temp->error_details);
		temp= temp->next;
		WebcfgDebug("count %d mp_count %zu\n", count, mp_count);
		if(count == (int)mp_count)
		{
			WebcfgDebug("Found all docs in the list\n");
			break;
		}
	}
	return;
}

char *replaceMacWord(const char *s, const char *macW, const char *deviceMACW)
{
	char *result = NULL;
	int i, cnt = 0;

	if(deviceMACW != NULL)
	{
		int deviceMACWlen = strlen(deviceMACW);
		int macWlen = strlen(macW);
		// Counting the number of times mac word occur in the string
		for (i = 0; s[i] != '\0'; i++)
		{
			if (strstr(&s[i], macW) == &s[i])
			{
			    cnt++;
			    // Jumping to index after the mac word.
			    i += macWlen - 1;
			}
		}

		result = (char *)malloc(i + cnt * (deviceMACWlen - macWlen) + 1);
		i = 0;
		while (*s)
		{
			if (strstr(s, macW) == s)
			{
				strcpy(&result[i], deviceMACW);
				i += deviceMACWlen;
				s += macWlen;
			}
			else
			    result[i++] = *s++;
		}
		result[i] = '\0';
	}
	return result;
}

void reqParam_destroy( int paramCnt, param_t *reqObj )
{
	int i = 0;
	if(reqObj)
	{
		for (i = 0; i < paramCnt; i++)
		{
			if(reqObj[i].name)
			{
				free(reqObj[i].name);
			}
			if(reqObj[i].value)
			{
				free(reqObj[i].value);
			}
		}
		free(reqObj);
	}
}

