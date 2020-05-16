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
#ifndef __WEBCFG_BLOB_H__
#define __WEBCFG_BLOB_H__

#include <stdint.h>

/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/
/* none */

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/

//for doc append
typedef struct appenddoc_struct{
    char * subdoc_name;
    uint32_t  version;
    uint16_t   transaction_id;
    size_t *count;
}appenddoc_t;

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

/**
 *  Packs portmapping append config doc.
 *
 *  @param 
 *
 *  @return 0 if the operation was a success, error otherwise
 */

ssize_t webcfg_pack_appenddoc(const appenddoc_t *appenddocData,void **data);


size_t appendWebcfgEncodedData( void **appendData, void *encodedBuffer, size_t encodedSize, void *metadataPack, size_t metadataSize );

char * webcfg_appendeddoc(char * subdoc_name, uint32_t version, char * blob_data, size_t blob_size);

uint16_t generateTransactionId();

int writeToFileData(char *db_file_path, char *data, size_t size);

#endif
