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
#ifndef __WEBCFG__DB_H__
#define __WEBCFG__DB_H__

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
/*----------------------------------------------------------------------------*/
/*                                   Macros                                   */
/*----------------------------------------------------------------------------*/

/*----------------------------------------------------------------------------*/
/*                               Data Structures                              */
/*----------------------------------------------------------------------------*/
struct {
	char * name,
	uint32_t version,
	char * status,
	int error_details // http error code/bus error code/component error code
} webconfig_db_t;

struct {
	webconfig_db_t *entries,
	size_t      entries_count;
} webconfig_db;

struct {
	char *data,
	size_t len
} blob;

/*----------------------------------------------------------------------------*/
/*                             External Functions                             */
/*----------------------------------------------------------------------------*/

/**
 *  Initialize/Load data from DB file.
 *
 *  @param db_file_path full path name
 *
 *  @return 
 */
int initDB(char * db_file_path);

int addNewDocEntry(webconfig_db_t *subdoc);

webconfig_db_t* getDocEntry(char *doc_name);

int updateDocEntry(char *doc_name, webconfig_db_t *subdoc);

int writeToDBFile(char * db_file_path);

blob * get_DB_BLOB();

char * get_DB_BLOB_base64();

#endif
