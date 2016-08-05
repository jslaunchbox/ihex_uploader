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
#include "shell-state.h"
#include "ihex-handler.h"

#define CMD_TRANSFER_IHEX  "ihex"
#define CMD_TRANSFER_RAW   "raw"
#define CMD_TRANSFER       "transfer"
#define CMD_FILENAME       "filename"
#define CMD_AT             "at"
#define CMD_SET            "set"
#define CMD_GET            "get"
#define CMD_READ           "read"
#define CMD_TEST           "test"
#define CMD_CLEAR          "clear"
#define CMD_BLUETOOTH      "bl"
#define CMD_HELP           "help"

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
const char EXCEDEED_SIZE[] = "String too long";
const char READY_FOR_RAW_DATA[] = "Ready for JavaScript." \
			"\tCtrl+Z or <EOF> to finish transfer.\r\n" \
			"\tCtrl+X or Ctrl+C to cancel.";

const char READY_FOR_IHEX_DATA[] = "[BEGIN IHEX]";

#define MAX_ARGUMENT_SIZE 32

int32_t ashell_help(const char *buf, uint32_t len) {
	acm_println("TODO: Read help file!");
	return RET_OK;
}

int32_t ashell_set_filename(const char *buf, uint32_t len) {
	uint32_t arg_len;

	if (len > MAX_NAME_SIZE) {
		acm_println(EXCEDEED_SIZE);
		return RET_ERROR;
	}

	buf = ashell_get_next_arg(buf, len, shell.filename, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return RET_ERROR;
	}

	acm_print("Filename [");
	acm_print(shell.filename);
	acm_println("]");
	return RET_OK;
}

int32_t ashell_read_data(const char *buf, uint32_t len, char *arg) {
	if (shell.state_flags & kShellTransferRaw) {
		acm_println(ANSI_CLEAR);
		acm_println(READY_FOR_RAW_DATA);
	}

	if (shell.state_flags & kShellTransferIhex) {
		acm_println(READY_FOR_IHEX_DATA);
		ihex_process_start();
	}
	return RET_OK;
}

int32_t ashell_set_transfer_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
	if (arg_len == 0) {
		acm_println(ERROR_NOT_ENOUGH_ARGUMENTS);
		return -1;
	}
	len -= arg_len;

	printf(" Arg [%s]::%d \n", arg, (int)arg_len);
	acm_println(arg);

	if (!strcmp(CMD_TRANSFER_RAW, arg)) {
		shell.state_flags |= kShellTransferRaw;
		shell.state_flags &= ~kShellTransferIhex;
		return RET_OK;
	}

	if (!strcmp(CMD_TRANSFER_IHEX, arg)) {
		shell.state_flags |= kShellTransferIhex;
		shell.state_flags &= ~kShellTransferRaw;
		return RET_OK;
	}

	return RET_UNKNOWN;
}

int32_t ashell_set_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
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

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
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

int32_t ashell_main_state(const char *buf, uint32_t len) {
	char arg[MAX_ARGUMENT_SIZE];
	uint32_t argc, arg_len = 0;

	printk("[ASHELL]");
	argc = ashell_get_argc(buf, len);
	printk("[ARGS %u]\n", argc);

	if (argc == 0)
		return 0;

	printk("[BOF]");
	printk("%s", buf);
	printk("[EOF]\n");

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
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
		acm_println("OK");
		return RET_OK;
	}

	if (!strcmp(CMD_CLEAR, arg)) {
		acm_print(ANSI_CLEAR);
		return RET_OK;
	}

	if (!strcmp(CMD_READ, arg)) {
		return ashell_read_data(buf, len, arg);
	}

	if (!strcmp(CMD_HELP, arg)) {
		return ashell_help(buf, len);
	}

#ifdef CONFIG_SHELL_UPLOADER_DEBUG
	printk("%u [%s] \n", arg_len, arg);

	for (int t = 0; t < argc; t++) {
		buf = ashell_get_next_arg(buf, len, arg, &arg_len);
		len -= arg_len;
		printf(" Arg [%s]::%d \n", arg, (int)arg_len);
	}
#endif
	return RET_UNKNOWN;
}