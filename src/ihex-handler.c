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
* @brief UART-driven uploader
*
* Reads a program from the uart using Intel HEX format.
*
* Designed to be used from Javascript or a ECMAScript object file.
*
* Hooks into the printk and fputc (for printf) modules. Poll driven.
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

#include <board.h>
#include <uart.h>
#include <toolchain.h>
#include <sections.h>
#include <atomic.h>
#include <misc/printk.h>

#include "code-memory.h"
#include "jerry-code.h"

#include "uart-uploader.h"
#include "ihex/kk_ihex_read.h"

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
static const char code_name[] = "ihex.js";

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
		print_acm("[EOF]");
		upload_state = UPLOAD_FINISHED;
	}
	return true;
}

/**************************** DEVICE **********************************/
/*
* Negotiate a re-upload
*/

void uart_handle_upload_error() {
	printf("[Download Error]\n");
}

/*
* Capture for the Intel Hex parser
*/
uint32_t process_init() {
	printf("[RDY]\n");
	ihex_begin_read(&ihex);
	code_memory = csopen(code_name, "w+");
}

uint32_t process_data(const char *buf, uint32_t len) {
	uint32_t processed = 0;
	while (len-- > 0) {
		processed++;
		char byte = *buf++;
#ifdef CONFIG_IHEX_UPLOADER_DEBUG
		write_data(&byte, 1);
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

bool ihex_process_is_finish() {
	return (upload_state == UPLOAD_FINISHED);
}

uint32_t ihex_process_finish() {
	if (upload_state != UPLOAD_FINISHED)
		return;

	printf("[EOF]\n");
	csclose(code_memory);
	ihex_end_read(&ihex);
	javascript_run_code(code_name);
	printf("[CLOSE]\n");
	return 0;
}
