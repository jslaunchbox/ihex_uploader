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

#define CMD_SET "set"
#define CMD_TEST "test"
#define CMD_CLEAR "clear"
#define CMD_TRANSFER "transfer"
#define CMD_BLUETOOTH "bl"

const char ERROR_NOT_ENOUGH_ARGUMENTS[] = "Not enough arguments";

#define MAX_ARGUMENT_SIZE 32

void ashell_set_transfer_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
	len -= arg_len;
	printf(" Arg [%s]::%d \n", arg, (int)arg_len);
}

void ashell_set_state(const char *buf, uint32_t len, char *arg) {
	uint32_t arg_len;
	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
	if (arg_len == 0) {
		printf(ERROR_NOT_ENOUGH_ARGUMENTS);
		return;
	}
	len -= arg_len;

	if (!strcmp(CMD_TRANSFER, arg)) {
		ashell_set_transfer_state(buf, len, arg);
	}
}

void ashell_main_state(const char *buf, uint32_t len) {
	char arg[MAX_ARGUMENT_SIZE];
	uint32_t argc, arg_len = 0;

	printk("[ASHELL]");

	argc = ashell_get_argc(buf, len);
	printk("[ARGS %u]\n", argc);

	if (argc == 0)
		return;

	printk("[BOF]");
	printk("%s", buf);
	printk("[EOF]\n");

	buf = ashell_get_next_arg(buf, len, arg, &arg_len);
	len -= arg_len;
	argc--;

	if (!strcmp(CMD_TEST, arg)) {
		acm_println("Hi world");
		return;
	}

	if (!strcmp(CMD_SET, arg)) {
		ashell_set_state(buf, len, arg);
		return;
	}

	if (!strcmp(CMD_CLEAR, arg)) {
		acm_print(ANSI_CLEAR);
		return 0;
	}

#ifdef CONFIG_SHELL_UPLOADER_DEBUG
	printk("%u [%s] \n", arg_len, arg);

	for (int t = 0; t < argc; t++) {
		buf = ashell_get_next_arg(buf, len, arg, &arg_len);
		len -= arg_len;
		printf(" Arg [%s]::%d \n", arg, (int)arg_len);
}
#endif
}