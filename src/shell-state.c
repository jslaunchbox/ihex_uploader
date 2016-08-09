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

int32_t ashell_print_file(const char *buf, uint32_t len, char *arg) {
	char data[READ_BUFFER_SIZE];
	const char *filename;
	CODE *file;
	uint32_t arg_len;
	size_t count;

	if (len > MAX_FILENAME_SIZE) {
		acm_println(ERROR_EXCEDEED_SIZE);
		return RET_ERROR;
	}

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_FILENAME_SIZE, &arg_len);
	if (arg_len == 0) {
		filename = shell.filename;
	} else {
		filename = arg;
	}

	file = csopen(filename, "r");

	// Error getting an id for our data storage
	if (!file) {
		acm_println(ERROR_FILE_NOT_FOUND);
		return RET_ERROR;
	}

	if (file->curend == 0) {
		acm_println("Empty file");
		csclose(file);
		return RET_ERROR;
	}

	csseek(file, 0, SEEK_SET);
	do {
		count = csread(data, 4, 1, file);
		for (int t = 0; t < count; t++) {
			if (data[t] == '\n')
				acm_write("\r\n", 2);
			else
				acm_writec(data[t]);
		}
	} while (count > 0);

	csclose(file);
	acm_println("");
	return RET_OK;
}

int32_t ashell_run_javascript(const char *buf, uint32_t len) {
	char filename[MAX_FILENAME_SIZE];
	uint32_t arg_len;

	if (len > MAX_FILENAME_SIZE) {
		acm_println(ERROR_EXCEDEED_SIZE);
		return RET_ERROR;
	}

	buf = ashell_get_next_arg_s(buf, len, filename, MAX_FILENAME_SIZE, &arg_len);
	if (arg_len == 0) {
		printk("[RUN][%s]\r\n", shell.filename);
		javascript_run_code(shell.filename);
		return RET_OK;
	}

	printk("[RUN][%s]\r\n", filename);
	javascript_run_code(filename);
	return RET_OK;
}

int32_t ashell_list_directory_contents(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
	if (arg_len == 0) {
		acm_println("TODO: Not implemented");
		return RET_OK;
	}

	CODE *file = csopen(arg, "r");
	printf("%5d %s\n", file->curend, arg);
	csclose(file);
	return RET_OK;
}

int32_t ashell_help(const char *buf, uint32_t len) {
	acm_println("TODO: Read help file!");
	return RET_OK;
}

int32_t ashell_set_filename(const char *buf, uint32_t len) {
	uint32_t arg_len;

	if (len > MAX_FILENAME_SIZE) {
		acm_println(ERROR_EXCEDEED_SIZE);
		return RET_ERROR;
	}

	buf = ashell_get_next_arg_s(buf, len, shell.filename, MAX_FILENAME_SIZE, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
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
	while (len > 0) {
		uint8_t byte = *buf++;
		if (!isprint(byte)) {
			switch (byte) {
				case ASCII_SUBSTITUTE:
					acm_println(MSG_FILE_SAVED);
					shell.state_flags &= ~kShellCaptureRaw;
					acm_set_prompt(NULL);
					ashell_close_capture();
					break;
				case ASCII_END_OF_TEXT:
				case ASCII_CANCEL:
					acm_println(MSG_FILE_ABORTED);
					shell.state_flags &= ~kShellCaptureRaw;
					acm_set_prompt(NULL);
					ashell_discard_capture();
					break;
				case ASCII_IF:
					acm_println("");
					break;
				default:
					printf("%c", byte);
			}
		} else {
			size_t written = cswrite(&byte, 1, 1, code_memory);
			if (written == 0) {
				printf("Failed writting into file \n");
				csdescribe(code_memory);
				return RET_ERROR;
			}
			printf("%c", byte);
		}
		len--;
	}
	return 0;
}

int32_t ashell_read_data(const char *buf, uint32_t len, char *arg) {
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

int32_t ashell_js_immediate_mode(const char *buf, uint32_t len) {
	shell.state_flags |= kShellEvalJavascript;
	acm_print(ANSI_CLEAR);
	acm_println(MSG_IMMEDIATE_MODE);
	acm_set_prompt(eval_prompt);
	return 0;
}

int32_t ashell_set_transfer_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	len -= arg_len;

	printf(" Arg [%s]::%d \n", arg, (int)arg_len);
	acm_println(arg);

	if (!strcmp(CMD_TRANSFER_RAW, arg)) {
		acm_set_prompt(NULL);
		shell.state_flags |= kShellTransferRaw;
		shell.state_flags &= ~kShellTransferIhex;
		return RET_OK;
	}

	if (!strcmp(CMD_TRANSFER_IHEX, arg)) {
		acm_set_prompt(hex_prompt);
		shell.state_flags |= kShellTransferIhex;
		shell.state_flags &= ~kShellTransferRaw;
		return RET_OK;
	}

	return RET_UNKNOWN;
}

int32_t ashell_set_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	len -= arg_len;

	if (!strcmp(CMD_TRANSFER, arg)) {
		return ashell_set_transfer_state(buf, len, arg);
	} else
		if (!strcmp(CMD_FILENAME, arg)) {
			return ashell_set_filename(buf, len);
		}

	return RET_UNKNOWN;
}

int32_t ashell_get_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	len -= arg_len;

	if (!strcmp(CMD_TRANSFER, arg)) {
		printf("Flags %lu\n", shell.state_flags);

		if (shell.state_flags & kShellTransferRaw)
			acm_println("Raw");

		if (shell.state_flags & kShellTransferIhex)
			acm_println("Ihex");

		return RET_OK;
	}

	if (!strcmp(CMD_FILENAME, arg)) {
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

int32_t ashell_main_state(const char *buf, uint32_t len) {
	char arg[MAX_ARGUMENT_SIZE];
	uint32_t argc, arg_len = 0;

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

	buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
	len -= arg_len;
	argc--;

	if (!strcmp(CMD_SET, arg)) {
		return ashell_set_state(buf, len, arg);
	}

	if (!strcmp(CMD_GET, arg)) {
		return ashell_get_state(buf, len, arg);
	}

	if (!strcmp(CMD_TEST, arg)) {
		acm_println("Hi world");
		return RET_OK;
	}

	if (!strcmp(CMD_AT, arg)) {
		printf("AT OK\r\n");
		acm_println("OK");
		return RET_OK;
	}

	if (!strcmp(CMD_CLEAR, arg)) {
		acm_print(ANSI_CLEAR);
		return RET_OK;
	}

	if (!strcmp(CMD_LOAD, arg)) {
		return ashell_read_data(buf, len, arg);
	}

	if (!strcmp(CMD_HELP, arg)) {
		return ashell_help(buf, len);
	}

	if (!strcmp(CMD_RUN, arg)) {
		return ashell_run_javascript(buf, len);
	}

	if (!strcmp(CMD_CAT, arg)) {
		return ashell_print_file(buf, len, arg);
	}

	if (!strcmp(CMD_LS, arg)) {
		return ashell_list_directory_contents(buf, len, arg);
	}

	if (!strcmp(CMD_EVAL, arg)) {
		return ashell_js_immediate_mode(buf, len);
	}

#ifdef CONFIG_SHELL_UPLOADER_DEBUG
	printk("%u [%s] \r\n", arg_len, arg);

	for (int t = 0; t < argc; t++) {
		buf = ashell_get_next_arg_s(buf, len, arg, MAX_ARGUMENT_SIZE, &arg_len);
		len -= arg_len;
		printf(" Arg [%s]::%d \n", arg, (int)arg_len);
	}
#endif
	return RET_UNKNOWN;
}