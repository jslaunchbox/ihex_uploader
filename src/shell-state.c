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
* @brief Shell to keep the different states of the machine
*
*/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <atomic.h>
#include <malloc.h>
#include <misc/printk.h>
#include <ctype.h>

#include "uart-uploader.h"
#include "acm-shell.h"
#include "ihex-handler.h"
#include "code-memory.h"
#include "shell-state.h"
#include "jerry-code.h"

#define CMD_TRANSFER_IHEX  "ihex"
#define CMD_TRANSFER_RAW   "raw"
#define CMD_TRANSFER       "transfer"
#define CMD_FILENAME       "filename"
#define CMD_AT             "at"
#define CMD_LS             "ls"
#define CMD_RUN            "run"
#define CMD_SET            "set"
#define CMD_GET            "get"
#define CMD_LOAD           "load"
#define CMD_TEST           "test"
#define CMD_CLEAR          "clear"
#define CMD_BLUETOOTH      "bl"
#define CMD_HELP           "help"
#define CMD_LOAD           "load"
#define CMD_CAT            "cat"
#define CMD_EVAL           "eval"
#define CMD_DU             "du"

/*
 * Contains the pointer to the memory where the code will be uploaded
 * using the stub interface at code_memory.c
 */
static CODE *code_memory = NULL;

/* Configuration of the callbacks to be called */
static struct shell_state_config shell = {
	.filename = "test.js",
	.state_flags = kShellTransferRaw
};

#define RET_OK      0
#define RET_ERROR   -1
#define RET_UNKNOWN -2

const char ERROR_NOT_RECOGNIZED[] = "Unknown command";
const char ERROR_NOT_ENOUGH_ARGUMENTS[] = "Not enough arguments";
const char ERROR_FILE_NOT_FOUND[] = "File not found";
const char ERROR_EXCEDEED_SIZE[] = "String too long";

const char MSG_FILE_SAVED[] = ANSI_FG_GREEN "Saving file. " ANSI_FG_RESTORE "run the 'run' command to see the result";
const char MSG_FILE_ABORTED[] = ANSI_FG_RED "Aborted!";
const char MSG_EXIT[] = ANSI_FG_GREEN "Back to shell!";

const char READY_FOR_RAW_DATA[] = "Ready for JavaScript. \r\n" \
"\tCtrl+Z or <EOF> to finish transfer.\r\n" \
"\tCtrl+X or Ctrl+C to cancel.";

const char MSG_IMMEDIATE_MODE[] = "Ready to evaluate JavaScript.\r\n" \
"\tCtrl+X or Ctrl+C to return to shell.";

const char READY_FOR_IHEX_DATA[] = "[BEGIN IHEX]";
const char hex_prompt[] = "HEX> ";
const char raw_prompt[] = ANSI_FG_YELLOW "RAW> " ANSI_FG_RESTORE;
const char eval_prompt[] = ANSI_FG_GREEN "js> " ANSI_FG_RESTORE;

#define MAX_ARGUMENT_SIZE 32

#define READ_BUFFER_SIZE 4

#ifndef CONFIG_IHEX_UPLOADER_DEBUG
#define DBG(...) { ; }
#else
#include <misc/printk.h>
#define DBG printk
#endif /* CONFIG_IHEX_UPLOADER_DEBUG */

#define CONFIG_USE_FILESYSTEM

#ifdef CONFIG_USE_FILESYSTEM
#include <fs/fs_interface.h>
#include <ff.h>
#include <fs/fat_fs.h>
#include <fs.h>
#endif

#ifdef CONFIG_USE_FILESYSTEM
ZFILE *fp;
#endif

const char *ashell_get_filename() {
	return shell.filename;
}

uint32_t ashell_get_filename_buffer(const char *buf, char *destination) {
	uint32_t arg_len = 0;
	uint32_t len = strlen(buf);
	if (len == 0)
		return RET_ERROR;

	buf = ashell_get_next_arg_s(buf, len, destination, MAX_FILENAME_SIZE, &arg_len);

	if (arg_len == 0) {
		strcpy(destination, shell.filename);
	}
	return arg_len;
}

int32_t ashell_list_dir(const char *buf) {
	int res;
	ZDIR dp;
	static struct zfs_dirent entry;

	char filename[MAX_FILENAME_SIZE];
	if (ashell_get_filename_buffer(buf, filename) <= 0) {
		return RET_ERROR;
	}

	res = fs_opendir(&dp, filename);
	if (res) {
		printf("Error opening dir[%d]\n", res);
		return res;
	}

	printf(ANSI_FG_LIGHT_BLUE "      .\n      ..\n" ANSI_FG_RESTORE);
	for (;;) {
		res = fs_readdir(&dp, &entry);

		/* entry.name[0] == 0 means end-of-dir */
		if (res || entry.name[0] == 0) {
			break;
		}
		if (entry.type == DIR_ENTRY_DIR) {
			printf(ANSI_FG_LIGHT_BLUE "%s\n" ANSI_FG_RESTORE, entry.name);
		}
		else {
			char *p = entry.name;
			for (; *p; ++p)
				*p = tolower((int) *p);

			printf("%5lu %s\n",
				entry.size, entry.name);
		}
	}

	fs_closedir(&dp);
	return 0;
}

int32_t ashell_print_file(const char *buf) {
	char filename[MAX_FILENAME_SIZE];
	char data[READ_BUFFER_SIZE];
	CODE *file;
	size_t count;

	if (ashell_get_filename_buffer(buf, filename) <= 0) {
		return RET_ERROR;
	}

	if (!csexist(filename)) {
		printf(ERROR_FILE_NOT_FOUND);
		return RET_ERROR;
	}

	printk("Open [%s]\n", filename);
	file = csopen(filename, "r");

	// Error getting an id for our data storage
	if (!file) {
		acm_println(ERROR_FILE_NOT_FOUND);
		return RET_ERROR;
	}

	ssize_t size = cssize(file);
	if (size == 0) {
		acm_println("Empty file");
		csclose(file);
		return RET_OK;
	}

	csseek(file, 0, SEEK_SET);
	do {
		count = csread(data, 4, 1, file);
		for (int t = 0; t < count; t++) {
			if (data[t] == '\n' || data[t] == '\r')
				acm_write("\r\n", 2);
			else
				acm_writec(data[t]);
		}
	} while (count > 0);

	csclose(file);
	return RET_OK;
}

int32_t ashell_run_javascript(char *buf) {
	char filename[MAX_FILENAME_SIZE];
	if (ashell_get_filename_buffer(buf, filename) <= 0) {
		return RET_ERROR;
	}

	printk("[RUN][%s]\r\n", filename);
	javascript_run_code(filename);
	return RET_OK;
}

int32_t ashell_disk_usage(char *buf) {
	char filename[MAX_FILENAME_SIZE];
	if (ashell_get_filename_buffer(buf, filename) <= 0) {
		return RET_ERROR;
	}

	CODE *file = csopen(filename, "r");
	if (file == NULL) {
		acm_println(ERROR_FILE_NOT_FOUND);
		return RET_ERROR;
	}

	ssize_t size = cssize(file);
	csclose(file);

	printf("%5ld %s\n", size, filename);
	return RET_OK;
}

int32_t ashell_help(const char *buf) {
	acm_println("TODO: Read help file!");
	return RET_OK;
}

int32_t ashell_set_filename(char *buf) {
	if (ashell_get_filename_buffer(buf, shell.filename) <= 0) {
		acm_print(ERROR_NOT_ENOUGH_ARGUMENTS);
		return RET_ERROR;
	}

	acm_print("Filename [");
	acm_print(shell.filename);
	acm_println("]");
	return RET_OK;
}

int32_t ashell_start_raw_capture() {
	code_memory = csopen(shell.filename, "w+");

	/* Error getting an id for our data storage */
	if (!code_memory) {
		return RET_ERROR;
	}

	return RET_OK;
}

int32_t ashell_close_capture() {
	return csclose(code_memory);
}

int32_t ashell_discard_capture() {
	csclose(code_memory);
	/* TODO: Delete file
	 return csdelete(shell.filename);
	*/
	return 0;
}

int32_t ashell_eval_javascript(const char *buf, uint32_t len) {
	const char *src = buf;

	while (len > 0) {
		uint8_t byte = *buf++;
		if (!isprint(byte)) {
			switch (byte) {
			case ASCII_SUBSTITUTE:
			case ASCII_END_OF_TEXT:
			case ASCII_CANCEL:
				acm_println(MSG_EXIT);
				shell.state_flags &= ~kShellEvalJavascript;
				acm_set_prompt(NULL);
				return 0;
			}
		}
		len--;
	}

	javascript_eval_code(src);
	return 0;
}

int32_t ashell_raw_capture(const char *buf, uint32_t len) {
	uint8_t eol = '\n';

	while (len > 0) {
		uint8_t byte = *buf++;
		if (!isprint(byte)) {
			switch (byte) {
				case ASCII_SUBSTITUTE:
					acm_println(MSG_FILE_SAVED);
					shell.state_flags &= ~kShellCaptureRaw;
					acm_set_prompt(NULL);
					cswrite(&eol, 1, 1, code_memory);
					ashell_close_capture();
					return RET_OK;
					break;
				case ASCII_END_OF_TEXT:
				case ASCII_CANCEL:
					acm_println(MSG_FILE_ABORTED);
					shell.state_flags &= ~kShellCaptureRaw;
					acm_set_prompt(NULL);
					ashell_discard_capture();
					break;
				case ASCII_CR:
				case ASCII_IF:
					acm_println("");
					break;
				default:
					printf("%c", byte);
			}
		} else {
			size_t written = cswrite(&byte, 1, 1, code_memory);
			if (written == 0) {
				return RET_ERROR;
			}
			printf("%c", byte);
		}
		len--;
	}

	cswrite(&eol, 1, 1, code_memory);
	return 0;
}

int32_t ashell_read_data(char *buf) {
	if (shell.state_flags & kShellTransferRaw) {
		acm_println(ANSI_CLEAR);
		acm_println(READY_FOR_RAW_DATA);
		acm_set_prompt(raw_prompt);
		shell.state_flags |= kShellCaptureRaw;
		ashell_start_raw_capture();
	}

	if (shell.state_flags & kShellTransferIhex) {
		acm_println(READY_FOR_IHEX_DATA);
		ashell_process_close();
	}
	return RET_OK;
}

int32_t ashell_js_immediate_mode(char *buf) {
	shell.state_flags |= kShellEvalJavascript;
	acm_print(ANSI_CLEAR);
	acm_println(MSG_IMMEDIATE_MODE);
	acm_set_prompt(eval_prompt);
	return 0;
}

int32_t ashell_set_transfer_state(char *buf) {
	char *next;
	if (buf == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	next = ashell_get_token_arg(buf);

	printf(" Arg [%s]\n", buf);
	acm_println(buf);

	if (!strcmp(CMD_TRANSFER_RAW, buf)) {
		acm_set_prompt(NULL);
		shell.state_flags |= kShellTransferRaw;
		shell.state_flags &= ~kShellTransferIhex;
		return RET_OK;
	}

	if (!strcmp(CMD_TRANSFER_IHEX, buf)) {
		acm_set_prompt(hex_prompt);
		shell.state_flags |= kShellTransferIhex;
		shell.state_flags &= ~kShellTransferRaw;
		return RET_OK;
	}

	return RET_UNKNOWN;
}

int32_t ashell_set_state(char *buf) {
	char *next;
	if (buf == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	next = ashell_get_token_arg(buf);

	if (!strcmp(CMD_TRANSFER, buf)) {
		return ashell_set_transfer_state(next);
	} else
		if (!strcmp(CMD_FILENAME, buf)) {
			return ashell_set_filename(next);
		}

	return RET_UNKNOWN;
}

int32_t ashell_get_state(char *buf) {
	char *next;

	if (buf == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}

	next = ashell_get_token_arg(buf);

	if (!strcmp(CMD_TRANSFER, buf)) {
		printf("Flags %lu\n", shell.state_flags);

		if (shell.state_flags & kShellTransferRaw)
			acm_println("Raw");

		if (shell.state_flags & kShellTransferIhex)
			acm_println("Ihex");

		return RET_OK;
	}

	if (!strcmp(CMD_FILENAME, buf)) {
		acm_println(shell.filename);
		return RET_OK;
	}

	return RET_UNKNOWN;
}

int32_t ashell_check_control(const char *buf, uint32_t len) {
	while (len > 0) {
		uint8_t byte = *buf++;
		if (!isprint(byte)) {
			switch (byte) {
				case ASCII_SUBSTITUTE:
					DBG("<CTRL + Z>");
					break;
			}
		}
		len--;
	}
	return 0;
}

int32_t ashell_main_state(char *buf, uint32_t len) {
	char *next;
	uint32_t argc;

	if (shell.state_flags & kShellEvalJavascript) {
		return ashell_eval_javascript(buf, len);
	}

	/* Capture data into the buffer */
	if (shell.state_flags & kShellCaptureRaw) {
		return ashell_raw_capture(buf, len);
	}

	DBG("[BOF]");
	DBG("%s", buf);
	DBG("[EOF]");
	ashell_check_control(buf, len);

	argc = ashell_get_argc(buf, len);
	DBG("[ARGS %u]\n", argc);

	if (argc == 0)
		return 0;

	buf[len] = '\0';
	buf = ashell_skip_spaces(buf);
	if (buf == NULL)
		return 0;

	next = ashell_get_token_arg(buf);

	if (!strcmp(CMD_SET, buf)) {
		return ashell_set_state(next);
	}

	if (!strcmp(CMD_GET, buf)) {
		return ashell_get_state(next);
	}

	if (!strcmp(CMD_TEST, buf)) {
		acm_println("Hi world");
		return RET_OK;
	}

	if (!strcmp(CMD_AT, buf)) {
		acm_println("OK");
		return RET_OK;
	}

	if (!strcmp(CMD_CLEAR, buf)) {
		acm_print(ANSI_CLEAR);
		return RET_OK;
	}

	if (!strcmp(CMD_LOAD, buf)) {
		return ashell_read_data(next);
	}

	if (!strcmp(CMD_HELP, buf)) {
		return ashell_help(next);
	}

	if (!strcmp(CMD_RUN, buf)) {
		return ashell_run_javascript(next);
	}

	if (!strcmp(CMD_CAT, buf)) {
		return ashell_print_file(next);
	}

	if (!strcmp(CMD_LS, buf)) {
		return ashell_list_dir(next);
	}

	if (!strcmp(CMD_EVAL, buf)) {
		return ashell_js_immediate_mode(next);
	}

	if (!strcmp(CMD_DU, buf)) {
		return ashell_disk_usage(next);
	}

	return RET_UNKNOWN;
}