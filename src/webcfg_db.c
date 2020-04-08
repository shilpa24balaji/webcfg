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
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <msgpack.h>

#include "webcfg_helpers.h"
#include "webcfg_multipart.h"
#include "webcfg_param.h"
#include "webcfg_log.h"
#include "webcfg_db.h"
#include "webcfg_generic.h"
#include "webcfg_pack.h"

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
enum {
    WD_OK                       = HELPERS_OK,
    WD_OUT_OF_MEMORY            = HELPERS_OUT_OF_MEMORY,
    WD_INVALID_FIRST_ELEMENT    = HELPERS_INVALID_FIRST_ELEMENT,
    WD_INVALID_DATATYPE,
    WD_INVALID_WD_OBJECT,
};

enum {
    BD_OK                       = HELPERS_OK,
    BD_OUT_OF_MEMORY            = HELPERS_OUT_OF_MEMORY,
    BD_INVALID_FIRST_ELEMENT    = HELPERS_INVALID_FIRST_ELEMENT,
    BD_INVALID_DATATYPE,
    BD_INVALID_BD_OBJECT,
};

/*----------------------------------------------------------------------------*/
/*                            File Scoped Variables                           */
/*----------------------------------------------------------------------------*/
static webconfig_tmp_data_t * g_head = NULL;
static blob_t * webcfgdb_blob = NULL;
static int numOfMpDocs = 0;
static int success_doc_count = 0;
/*----------------------------------------------------------------------------*/
/*                             Function Prototypes                            */
/*----------------------------------------------------------------------------*/
int process_webcfgdbparams( webconfig_db_data_t *e, msgpack_object_map *map );
int process_webcfgdb( webconfig_db_data_t *wd, msgpack_object *obj );

int process_webcfgdbblob( blob_struct_t *bd, msgpack_object *obj );
int process_webcfgdbblobparams( blob_data_t *e, msgpack_object_map *map );

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

//To initialize the DB when DB file is present
int initDB(char * db_file_path )
{
     FILE *fp;
     char *data;
     size_t len;
     int ch_count=0;
     //size_t k;
     WebConfigLog("DB file path is %s\n", db_file_path);
     fp = fopen(db_file_path,"rb");

     if (fp == NULL)
     {
	WebConfigLog("Failed to open file %s\n", db_file_path);
	return 0;
     }
     
     fseek(fp, 0, SEEK_END);
     ch_count = ftell(fp);
     fseek(fp, 0, SEEK_SET);
     data = (char *) malloc(sizeof(char) * (ch_count + 1));
     fread(data, 1, ch_count,fp);
     len = ch_count;
     fclose(fp);

     decodeData((void *)data, len);
     return 1;
     
}

//addNewDocEntry function will pack the DB blob and persist to a bin file
int addNewDocEntry(size_t count)
{    
     size_t webcfgdbPackSize = -1;
     void* data = NULL;
 
     WebConfigLog("size of subdoc %ld\n", (size_t)count);
     webcfgdbPackSize = webcfgdb_pack(webcfgdb_data, &data, count);
     WebConfigLog("size of webcfgdbPackSize %ld\n", webcfgdbPackSize);
     WebConfigLog("writeToDBFile %s\n", WEBCFG_DB_FILE);
     writeToDBFile(WEBCFG_DB_FILE,(char *)data);
  
     return 0;
}

//generateBlob function is used to pack webconfig_tmp_data_t and webconfig_db_data_t
int generateBlob()
{
    size_t webcfgdbBlobPackSize = -1;
    void * data = NULL;

    webcfgdbBlobPackSize = webcfgdb_blob_pack(webcfgdb_data, g_head, &data);
    webcfgdb_blob = (blob_t *)malloc(sizeof(blob_t));
    webcfgdb_blob->data = (char *)data;
    webcfgdb_blob->len  = webcfgdbBlobPackSize;

    WebConfigLog("The webcfgdbBlobPackSize is : %ld\n",webcfgdb_blob->len);
    //WebConfigLog("The value of blob is %s\n",webcfgdb_blob->data);
    return 1;
}

int writeToDBFile(char *db_file_path, char *data)
{
	FILE *fp;
	fp = fopen(db_file_path , "w+");
	if (fp == NULL)
	{
		WebConfigLog("Failed to open file in db %s\n", db_file_path );
		return 0;
	}
	if(data !=NULL)
	{
		fwrite(data, strlen(data), 1, fp);
		fclose(fp);
		return 1;
	}
	else
	{
		WebConfigLog("WriteToJson failed, Data is NULL\n");
		return 0;
	}
}

int readBlobFromFile(char * blob_file_path)
{
     FILE *fp;
     char *data;
     size_t len;
     int ch_count=0;
     size_t k;
     WebConfigLog("DB file path is %s\n", blob_file_path);
     fp = fopen(blob_file_path,"rb");

     if (fp == NULL)
     {
	WebConfigLog("Failed to open file %s\n", blob_file_path);
	return 0;
     }
     
     fseek(fp, 0, SEEK_END);
     ch_count = ftell(fp);
     fseek(fp, 0, SEEK_SET);
     data = (char *) malloc(sizeof(char) * (ch_count + 1));
     fread(data, 1, ch_count,fp);
     len = ch_count;
     fclose(fp);

     blob_struct_t *bd = NULL;
     bd = ( blob_struct_t * ) malloc( sizeof( blob_struct_t ) );
     bd = decodeBlobData((void *)data, len);
     WebConfigLog("Size of webcfgdbblob %ld\n", (size_t)bd);
     if(bd != NULL)
     {   
         
         for(k = 0;k< bd->entries_count ; k++)
         {   
             WebConfigLog("bd->entries[%zu].name %s\n", k, bd->entries[k].name);
	     WebConfigLog("bd->entries[%zu].version %lu\n" ,k,  (long)bd->entries[k].version); 
             WebConfigLog("bd->entries[%zu].status %s\n", k, bd->entries[k].status);
         }

               
     }
       webcfgdbblob_destroy(bd);
     return 1;
}

int writeBlobToFile(char *blob_file_path, char *data)
{
	FILE *fp;
	fp = fopen(blob_file_path , "w+");
	if (fp == NULL)
	{
		WebConfigLog("Failed to open file in db %s\n", blob_file_path );
		return 0;
	}
	if(data !=NULL)
	{
		fwrite(data, strlen(data), 1, fp);
		fclose(fp);
		return 1;
	}
	else
	{
		WebConfigLog("WriteToJson failed, Data is NULL\n");
		return 0;
	}
}

//Used to decode the DB bin file 
webconfig_db_data_t* decodeData(const void * buf, size_t len)
{
     return helper_convert( buf, len, sizeof(webconfig_db_data_t),"webcfgdb",
                           MSGPACK_OBJECT_ARRAY, true,
                           (process_fn_t) process_webcfgdb,
                           NULL );
}

//Used to decode the webconfig_tmp_data_t and webconfig_db_data_t msgpack
blob_struct_t* decodeBlobData(const void * buf, size_t len)
{
     return helper_convert( buf, len, sizeof(blob_struct_t),"webcfgblob",
                           MSGPACK_OBJECT_ARRAY, true,
                           (process_fn_t) process_webcfgdbblob,
                           (destroy_fn_t) webcfgdbblob_destroy );
}


void webcfgdbblob_destroy( blob_struct_t *bd )
{
    if( NULL != bd ) {
        size_t i;
        for( i = 0; i < bd->entries_count; i++ ) {
            if( NULL != bd->entries[i].name ) {
                free( bd->entries[i].name );
            }
            if( NULL != bd->entries[i].status ) {
                free( bd->entries[i].status );
            }
	    
        }
        if( NULL != bd->entries ) {
            free( bd->entries );
        }
        free( bd );
    }
}

const char* webcfgdbblob_strerror( int errnum )
{
    struct error_map {
        int v;
        const char *txt;
    } map[] = {
        { .v = BD_OK,                               .txt = "No errors." },
        { .v = BD_OUT_OF_MEMORY,                    .txt = "Out of memory." },
        { .v = BD_INVALID_FIRST_ELEMENT,            .txt = "Invalid first element." },
        { .v = BD_INVALID_DATATYPE,                 .txt = "Invalid 'datatype' value." },
        { .v = BD_INVALID_BD_OBJECT,                .txt = "Invalid 'parameters' array." },
        { .v = 0, .txt = NULL }
    };
    int i = 0;

    while( (map[i].v != errnum) && (NULL != map[i].txt) ) { i++; }

    if( NULL == map[i].txt ) {
        return "Unknown error.";
    }

    return map[i].txt;
}

const char* webcfgdbparam_strerror( int errnum )
{
    struct error_map {
        int v;
        const char *txt;
    } map[] = {
        { .v = WD_OK,                               .txt = "No errors." },
        { .v = WD_OUT_OF_MEMORY,                    .txt = "Out of memory." },
        { .v = WD_INVALID_FIRST_ELEMENT,            .txt = "Invalid first element." },
        { .v = WD_INVALID_DATATYPE,                 .txt = "Invalid 'datatype' value." },
        { .v = WD_INVALID_WD_OBJECT,                .txt = "Invalid 'parameters' array." },
        { .v = 0, .txt = NULL }
    };
    int i = 0;

    while( (map[i].v != errnum) && (NULL != map[i].txt) ) { i++; }

    if( NULL == map[i].txt ) {
        return "Unknown error.";
    }

    return map[i].txt;
}

webconfig_db_data_t * get_global_db_node(void)
{
    return webcfgdb_data;
}

webconfig_tmp_data_t * get_global_tmp_node(void)
{
    return g_head;
}

int get_numOfMpDocs()
{
    return numOfMpDocs;
}

int get_successDocCount()
{
    return success_doc_count;
}

blob_t * get_DB_BLOB()
{
     return webcfgdb_blob;
}

//new_node indicates the docs which need to be added to list
int addToTmpList( multipart_t *mp)
{
	int m = 0;
	int retStatus = 0;

	WebConfigLog("mp->entries_count is %zu\n", mp->entries_count);
	numOfMpDocs = 0;
	WebConfigLog("reset numOfMpDocs to %d\n", numOfMpDocs);
	WebConfigLog("get_global_root is %lu\n", (long)get_global_root());

	delete_tmp_doc_list();
	WebConfigLog("Deleted existing tmp list, proceed to addToTmpList\n");

	for(m = 0 ; m<((int)mp->entries_count); m++)
	{
		webconfig_tmp_data_t *new_node;
		new_node=(webconfig_tmp_data_t *)malloc(sizeof(webconfig_tmp_data_t));
		if(new_node)
		{
			memset( new_node, 0, sizeof( webconfig_tmp_data_t ) );

			if(numOfMpDocs == 0)
			{
				WebConfigLog("Adding root doc to list\n");
				new_node->name = strdup("root");
				new_node->version = get_global_root();
				new_node->status = strdup("pending");
			}
			else
			{
				new_node->name = strdup(mp->entries[m-1].name_space);
				WebConfigLog("mp->entries[m-1].name_space is %s\n", mp->entries[m-1].name_space);
				new_node->version = mp->entries[m-1].etag;
				new_node->status = strdup("pending");
			}

			WebConfigLog("new_node->name is %s\n", new_node->name);
			WebConfigLog("new_node->version is %lu\n", (long)new_node->version);
			WebConfigLog("new_node->status is %s\n", new_node->status);


			new_node->next=NULL;

			if (g_head == NULL)
			{
				g_head = new_node;
			}
			else
			{
				webconfig_tmp_data_t *temp = NULL;
				WebConfigLog("Adding docs to list\n");
				temp = g_head;
				while(temp->next !=NULL)
				{
					temp=temp->next;
				}
				temp->next=new_node;
			}

			WebConfigLog("--->>doc %s with version %lu is added to list\n", new_node->name, (long)new_node->version);
			numOfMpDocs = numOfMpDocs + 1;
		}
		WebConfigLog("numOfMpDocs %d\n", numOfMpDocs);

		if((int)mp->entries_count == numOfMpDocs)
		{
			WebConfigLog("addToTmpList success\n");
			retStatus = 1;
		}
	}
	WebConfigLog("addToList return %d\n", retStatus);
	return retStatus;
}

//update version, status for each doc
int updateTmpList(char *docname, uint32_t version, char *status)
{
	webconfig_tmp_data_t *temp = NULL;
	temp = get_global_tmp_node();

	//Traverse through doc list & update required doc
	while (NULL != temp)
	{
		WebConfigLog("node is pointing to temp->name %s \n",temp->name);
		if( strcmp(docname, temp->name) == 0)
		{
			temp->version = version;
			temp->status = strdup(status);
			WebConfigLog("-->>doc %s is updated to version %lu status %s\n", docname, (long)temp->version, temp->status);
			return 1;
		}
		temp= temp->next;
	}
	return 0;
}


//delete doc from webcfg Tmp list
int deleteFromTmpList(char* doc_name)
{
	webconfig_tmp_data_t *prev_node = NULL, *curr_node = NULL;

	if( NULL == doc_name )
	{
		WebConfigLog("Invalid value for doc\n");
		return -1;
	}
	WebConfigLog("doc to be deleted: %s\n", doc_name);

	prev_node = NULL;
	curr_node = g_head ;

	// Traverse to get the doc to be deleted
	while( NULL != curr_node )
	{
		if(strcmp(curr_node->name, doc_name) == 0)
		{
			WebConfigLog("Found the node to delete\n");
			if( NULL == prev_node )
			{
				WebConfigLog("need to delete first doc\n");
				g_head = curr_node->next;
			}
			else
			{
				WebConfigLog("Traversing to find node\n");
				prev_node->next = curr_node->next;
			}

			WebConfigLog("Deleting the node\n");
			free( curr_node );
			curr_node = NULL;
			WebConfigLog("Deleted successfully and returning..\n");
			numOfMpDocs =numOfMpDocs - 1;
			WebConfigLog("numOfMpDocs after delete is %d\n", numOfMpDocs);
			return 0;
		}

		prev_node = curr_node;
		curr_node = curr_node->next;
	}
	WebConfigLog("Could not find the entry to delete from list\n");
	return -1;
}

void delete_tmp_doc_list()
{
   webconfig_tmp_data_t *temp = NULL;
   webconfig_tmp_data_t *head = NULL;
   head = get_global_tmp_node();

    while(head != NULL)
    {
        temp = head;
        head = head->next;
	WebConfigLog("Delete node--> temp->name %s temp->version %lu temp->status %s\n",temp->name, (long)temp->version, temp->status);
        free(temp);
	temp = NULL;
    }
    g_head = NULL;
    WebConfigLog("Deleted all docs from tmp list\n");
}
/*----------------------------------------------------------------------------*/
/*                             Internal functions                             */
/*----------------------------------------------------------------------------*/

/**
 *  Convert the msgpack map into the webconfig_db_data_t structure.
 *
 *  @param e    the entry pointer
 *  @param map  the msgpack map pointer
 *
 *  @return 0 on success, error otherwise
 */
int process_webcfgdbparams( webconfig_db_data_t *e, msgpack_object_map *map )
{
    int left = map->size;
    uint8_t objects_left = 0x02;
    msgpack_object_kv *p;

    p = map->ptr;
    while( (0 < objects_left) && (0 < left--) )
    {
        if( MSGPACK_OBJECT_STR == p->key.type )
        {
            if( MSGPACK_OBJECT_POSITIVE_INTEGER == p->val.type )
            {
                if( 0 == match(p, "version") )
                {
                    if( UINT32_MAX < p->val.via.u64 )
                    {
			//WebConfigLog("e->type is %d\n", e->type);
                        errno = WD_INVALID_DATATYPE;
                        return -1;
                    }
                    else
                    {
                        e->version = (uint32_t) p->val.via.u64;
			//WebConfigLog("e->version is %lu\n", (long)e->version);
                    }
                    objects_left &= ~(1 << 1);
		    //WebConfigLog("objects_left after datatype %d\n", objects_left);
                }
                
            }
            else if( MSGPACK_OBJECT_STR == p->val.type )
            {
                if( 0 == match(p, "name") )
                {
                    e->name = strndup( p->val.via.str.ptr, p->val.via.str.size );
		    //WebConfigLog("e->name is %s\n", e->name);
                    objects_left &= ~(1 << 0);
		    //WebConfigLog("objects_left after name %d\n", objects_left);
                }
            }
        }
        p++;
    }

    if( 1 & objects_left )
    {
    }
    else
    {
        errno = WD_OK;
    }

    return (0 == objects_left) ? 0 : -1;
}


int process_webcfgdb( webconfig_db_data_t *wd, msgpack_object *obj )
{
    //WebConfigLog(" process_webcfgdbparam \n");
    msgpack_object_array *array = &obj->via.array;
    if( 0 < array->size )
    {
        size_t i;
        size_t entries_count = -1;

        entries_count = array->size;
        webcfgdb_data = NULL;
        WebConfigLog("entries_count %zu\n",entries_count);
        for( i = 0; i < entries_count; i++ )
        {
            wd = (webconfig_db_data_t *) malloc (sizeof(webconfig_db_data_t));   
            if( MSGPACK_OBJECT_MAP != array->ptr[i].type )
            {
                errno = WD_INVALID_WD_OBJECT;
                return -1;
            }
            if( 0 != process_webcfgdbparams(wd, &array->ptr[i].via.map) )
            {
		//WebConfigLog("process_webcfgdbparam failed\n");
                return -1;
            }
            else
            {
                wd->next = NULL;
                addToDBList(wd);
               
            }
        }
    }

    return 0;
}

void addToDBList(webconfig_db_data_t *webcfgdb)
{
      if(webcfgdb_data == NULL)
      {
          webcfgdb_data = webcfgdb;
          WebConfigLog("Producer added webcfgdb\n");
          success_doc_count++;
          webcfgdb = NULL;
      }
      else
      {
          webconfig_db_data_t *temp = NULL;
          temp = webcfgdb_data;
          while(temp->next)
          {   
              temp = temp->next;
          }
          temp->next = webcfgdb;
          success_doc_count++;
         
      }
}

char * get_DB_BLOB_base64()
{
    char* b64buffer =  NULL;
    size_t encodeSize = 0;
    blob_t * db_blob = get_DB_BLOB();

    	WebConfigLog("-----------Start of Base64 Encode ------------\n");
	encodeSize = b64_get_encoded_buffer_size( db_blob->len );
	WebConfigLog("encodeSize is %d\n", encodeSize);
	b64buffer = malloc(encodeSize + 1);
	b64_encode((uint8_t *)db_blob->data, db_blob->len, (uint8_t *)b64buffer);
	b64buffer[encodeSize] = '\0' ;
	WebConfigLog("b64buffer is %s\n", b64buffer);
	WebConfigLog("---------- End of Base64 Encode -------------\n");
    
    return b64buffer;
}

/**char * get_DB_BLOB_base64(size_t *len)
{
    char * decodeMsg = NULL;
    blob_t * temp_blob = (blob_t *)malloc(sizeof(blob_t));
    temp_blob = get_DB_BLOB();
    *len = temp_blob->len; 
    b64_encoder(temp_blob->data, temp_blob->len, &decodeMsg);
    return decodeMsg;
}*/

void b64_encoder(const void *buf,size_t len, char ** decodeMsg)
{
        char* b64buffer =  NULL;
	size_t encodeSize = 0;
        size_t size =0;
	size_t decodeMsgSize =0;

	WebConfigLog("-----------Start of Base64 Encode ------------\n");
	encodeSize = b64_get_encoded_buffer_size( len );
	WebConfigLog("encodeSize is %ld\n", encodeSize);
	b64buffer = malloc(encodeSize+1);
	b64_encode((const uint8_t *)buf, len, (uint8_t *)b64buffer);
	b64buffer[encodeSize] = '\0' ;
	WebConfigLog("---------- End of Base64 Encode -------------\n");

	WebConfigLog("Final Encoded data length: %ld\n",strlen(b64buffer));
	/*********** base64 encode *****************/

	//Start of b64 decoding
	WebConfigLog("----Start of b64 decoding----\n");
	decodeMsgSize = b64_get_decoded_buffer_size(strlen(b64buffer));
	WebConfigLog("expected b64 decoded msg size : %ld bytes\n",decodeMsgSize);

	*decodeMsg = (char *) malloc(sizeof(char) * decodeMsgSize);

	size = b64_decode( (const uint8_t *)b64buffer, strlen(b64buffer), (uint8_t *)*decodeMsg );
	WebConfigLog("base64 decoded data containing %ld bytes\n",size);
        
	WebConfigLog("----End of b64 decoding----\n");

	//End of b64 decoding

}


/* For Blob decode purpose */

int process_webcfgdbblobparams( blob_data_t *e, msgpack_object_map *map )
{
    int left = map->size;
    uint8_t objects_left = 0x03;
    msgpack_object_kv *p;

    p = map->ptr;
    while( (0 < objects_left) && (0 < left--) )
    {
        if( MSGPACK_OBJECT_STR == p->key.type )
        {
            if( MSGPACK_OBJECT_POSITIVE_INTEGER == p->val.type )
            {
                if( 0 == match(p, "version") )
                {
                    if( UINT32_MAX < p->val.via.u64 )
                    {
			//WebConfigLog("e->type is %d\n", e->type);
                        errno = BD_INVALID_DATATYPE;
                        return -1;
                    }
                    else
                    {
                        e->version = (uint32_t) p->val.via.u64;
			//WebConfigLog("e->version is %lu\n", (long)e->version);
                    }
                    objects_left &= ~(1 << 1);
		    //WebConfigLog("objects_left after datatype %d\n", objects_left);
                }
                
            }
            else if( MSGPACK_OBJECT_STR == p->val.type )
            {
                if( 0 == match(p, "name") )
                {
                    e->name = strndup( p->val.via.str.ptr, p->val.via.str.size );
		    //WebConfigLog("e->name is %s\n", e->name);
                    objects_left &= ~(1 << 2);
		    //WebConfigLog("objects_left after name %d\n", objects_left);
                }
                else if(0 == match(p, "status") )
                {
                     e->status = strndup( p->val.via.str.ptr, p->val.via.str.size );
		     //WebConfigLog("e->status is %s\n", e->status);
                     objects_left &= ~(1 << 0);
                } 
            }
        }
        p++;
    }

    if( 1 & objects_left )
    {
    }
    else
    {
        errno = BD_OK;
    }

    return (0 == objects_left) ? 0 : -1;
}

int process_webcfgdbblob( blob_struct_t *bd, msgpack_object *obj )
{
    WebConfigLog(" process_webcfgdbblob \n");
    msgpack_object_array *array = &obj->via.array;
    if( 0 < array->size )
    {
        size_t i;

        bd->entries_count = array->size;
        bd->entries = (blob_data_t *) malloc( sizeof(blob_data_t) * bd->entries_count );
        if( NULL == bd->entries )
        {
            bd->entries_count = 0;
            return -1;
        }
        
        WebConfigLog("bd->entries_count %zu\n",bd->entries_count);
        memset( bd->entries, 0, sizeof(blob_data_t) * bd->entries_count );
        for( i = 0; i < bd->entries_count; i++ )
        {
            if( MSGPACK_OBJECT_MAP != array->ptr[i].type )
            {
                errno = BD_INVALID_BD_OBJECT;
                return -1;
            }
            if( 0 != process_webcfgdbblobparams(&bd->entries[i], &array->ptr[i].via.map) )
            {
		WebConfigLog("process_webcfgdbblobparam failed\n");
                return -1;
            }
        }
    }

    return 0;
}
