/*
* Copyright (c) 2011-2012, 2014-2015 Wind River Systems, Inc.
*
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*/

/**
* @file
* @brief Intel Hex handler
*
* Reads a program from the uart using Intel HEX format.
* Designed to be used from Javascript or a ECMAScript object file.

*/

#include <nanokernel.h>

#include <arch/cpu.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <malloc.h>
#include <errno.h>
#include <ctype.h>

#include <device.h>
#include <init.h>

#include <toolchain.h>
#include <sections.h>
#include <atomic.h>
#include <misc/printk.h>

#include "code-memory.h"
#include "jerry-code.h"

#include "uart-uploader.h"
#include "ihex/kk_ihex_read.h"

#define CONFIG_IHEX_UPLOADER_DEBUG

#ifndef CONFIG_IHEX_UPLOADER_DEBUG
#define DBG(...) { ; }
#else
#if defined(CONFIG_STDOUT_CONSOLE)
#include <stdio.h>
#define DBG printf
#else
#include <misc/printk.h>
#define DBG printk
#endif /* CONFIG_STDOUT_CONSOLE */
#endif /* CONFIG_IHEX_UPLOADER_DEBUG */

/*
 * Contains the pointer to the memory where the code will be uploaded
 * using the stub interface at code_memory.c
 */
static CODE *code_memory = NULL;

/* Filename where to store this data */
static const char *code_name;

/****************************** IHEX ****************************************/

static bool marker = false;
static struct ihex_state ihex;

static int8_t upload_state = 0;
#define UPLOAD_START       0
#define UPLOAD_IN_PROGRESS 1
#define UPLOAD_FINISHED    2
#define UPLOAD_ERROR       -1

/* Data received from the buffer */
ihex_bool_t ihex_data_read(struct ihex_state *ihex,
	ihex_record_type_t type,
	ihex_bool_t checksum_error) {
	if (checksum_error) {
		upload_state = UPLOAD_ERROR;
		printf("[ERR] Checksum_error\n");
		return false;
	};

	if (type == IHEX_DATA_RECORD) {
		upload_state = UPLOAD_IN_PROGRESS;
		unsigned long address = (unsigned long)IHEX_LINEAR_ADDRESS(ihex);
		ihex->data[ihex->length] = 0;

		DBG("%d::%d:: %s \n", (int)address, ihex->length, ihex->data);

		csseek(code_memory, address, SEEK_SET);
		cswrite(ihex->data, ihex->length, 1, code_memory);
	}
	else if (type == IHEX_END_OF_FILE_RECORD) {
		acm_println("[EOF]");
		upload_state = UPLOAD_FINISHED;
	}
	return true;
}

/**************************** DEVICE **********************************/
/*
* Negotiate a re-upload
*/
void ihex_process_error(uint32_t error) {
	printf("[Download Error]\n");
}

/*
* Capture for the Intel Hex parser
*/
uint32_t ihex_process_init(const char *filename) {
	upload_state = UPLOAD_START;
	printf("[RDY]\n");
	ihex_begin_read(&ihex);

	code_name = filename;
	code_memory = csopen(code_name, "w+");

	// Error getting an id for our data storage
	if (!code_memory) {
		upload_state = UPLOAD_ERROR;
	}

	return (!code_memory);
}

uint32_t ihex_process_data(const char *buf, uint32_t len) {
	uint32_t processed = 0;
	while (len-- > 0) {
		processed++;
		char byte = *buf++;
#ifdef CONFIG_IHEX_UPLOADER_DEBUG
		acm_write(&byte, 1);
#endif
		if (marker) {
			ihex_read_byte(&ihex, byte);
		}

		switch (byte) {
			case ':':
				DBG("<MK>");
				ihex_read_byte(&ihex, byte);
				marker = true;
				break;
			case '\r':
				marker = false;
				DBG("<CR>");
				break;
			case '\n':
				marker = false;
				DBG("<IF>");
				break;
		}
	}
	return processed;
}

bool ihex_process_is_done() {
	return (upload_state == UPLOAD_FINISHED || upload_state == UPLOAD_ERROR);
}

uint32_t ihex_process_finish() {
	if (upload_state == UPLOAD_ERROR) {
		printf("[Error] Callback handle error \n");
		return 1;
	}

	if (upload_state != UPLOAD_FINISHED)
		return 1;

	printf("[EOF]\n");
	csclose(code_memory);
	ihex_end_read(&ihex);
	javascript_run_code(code_name);
	printf("[CLOSE]\n");
	return 0;
}

void ihex_print_status() {
	if (code_memory != NULL) {
		printf("[CODE START]\n");
		printf((char *)code_memory);
		printf("[CODE END]\n");
	}

	if (marker)
		printf("[Marker]\n");
}

void ihex_process_start() {
	struct uploader_cfg_data cfg;

	cfg.cb_status = NULL;
	cfg.interface.init_cb = ihex_process_init;
	cfg.interface.error_cb = ihex_process_error;
	cfg.interface.is_done = ihex_process_is_done;
	cfg.interface.close_cb = ihex_process_finish;
	cfg.interface.process_cb = ihex_process_data;
	cfg.print_state = ihex_print_status;

	process_set_config(&cfg);
}
