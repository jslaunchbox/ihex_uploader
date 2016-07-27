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
#include <malloc.h>
#include <misc/printk.h>
#include "uart-uploader.h"

#define CONFIG_SHELL_UPLOADER_DEBUG

#ifndef CONFIG_SHELL_UPLOADER_DEBUG
#define DBG(...) { ; }
#else
#if defined(CONFIG_STDOUT_CONSOLE)
#define DBG printf
#else
#define DBG printk
#endif /* CONFIG_STDOUT_CONSOLE */
#endif /* CONFIG_SHELL_UPLOADER_DEBUG */

#define MAX_LINE 128
#define MAX_ARGUMENT_SIZE 32
static char *shell_line;
static uint8_t tail;

/*
* Returns the number of arguments on the string
* str   Null terminated string
* nsize Check for size boundaries
*/
uint32_t shell_get_argc(const char *str, uint32_t nsize) {
	if (str == NULL || nsize == 0 || *str == '\0')
		return 0;

	uint32_t size = 1;
	bool div = false;

	// Skip the first spaces
	while (nsize-- > 0 && *str != 0 && *str == ' ') {
		str++;
		if (size) {
			size = 0;
			div = true;
		}
	}

	while (nsize-- > 0 && *str++ != 0) {
		if (*str == ' ')
			div = true;

		if (div && *str != ' ' && *str != '\0') {
			div = false;
			size++;
		}
	}

	return size;
}

/* Copies the next argument into the string
* str     Null terminated string
* nsize   Checks line size boundaries.
* str_arg Initialized destination for the argument
* length  Returns length of the argument found.
*/

const char *shell_get_next_arg(const char *str, uint32_t nsize, char *str_arg, uint32_t *length) {
	*length = 0;
	if (nsize == 0 || str == NULL || *str == '\0') {
		str_arg[0] = '\0';
		return 0;
	}

	/* Skip spaces */
	while (nsize-- > 0 && *str != 0 && *str == ' ') {
		str++;
	}

	while (nsize-- >= 0 && *str != ' ') {
		*str_arg++ = *str++;
		(*length)++;
		if (*str == 0)
			break;
	}

	*str_arg = '\0';
	return str;
}

uint32_t shell_process_init(const char *filename) {
	printf("[SHELL] Init\n");
	shell_line = NULL;
	return 0;
}

void shell_process_line(const char *buf, uint32_t len) {
	char arg[MAX_ARGUMENT_SIZE];
	uint32_t argc, arg_len;

	printf("** Line found **\n");
	printf("%s", buf);

	argc = shell_get_argc(buf, len);
	for (int t = 0; t < argc; t++) {
		buf = shell_get_next_arg(buf, len, arg, &arg_len);
		printf("[%s]::%d \n", buf, (int) len);
		printf(" Arg [%s]::%d \n", arg, (int) arg_len);
	}
}

uint32_t shell_process_data(const char *buf, uint32_t len) {
	uint32_t processed = 0;
	bool eof = false;
	bool flush_line = false;

	if (shell_line == NULL) {
		printk("[Proccess]%d\n", (int)len);
		printk("[%s]\n", buf);
		shell_line = (char *)malloc(MAX_LINE);
		tail = 0;
	}

	while (len-- > 0) {
		processed++;
		char byte = *buf++;

		if (tail == MAX_LINE) {
			DBG("Line size exceeded \n");
			tail = 0;
		}

		if (eof && byte != '\n') {
			flush_line = true;
		} else {
			shell_line[tail] = byte;
		}

		acm_write(&byte, 1);

		switch (byte) {
			case '\r':
				DBG("<CR>");
				eof = true;
				break;
			case '\n':
				DBG("<IF>");
				flush_line = true;
				break;
		}

		if (flush_line) {
			shell_process_line(shell_line, tail);
			flush_line = false;
			tail = 0;

			// Detected <CR> without <IF>
			if (eof)
				shell_line[0] = byte;

			eof = false;
		}
	}

	// Done processing line
	if (tail == 0 && shell_line != NULL) {
		printk("[Free]\n");
		free(shell_line);
		shell_line = NULL;
	}
	return processed;
}

bool shell_process_is_done() {
	return false;
}

uint32_t shell_process_finish() {
	return 0;
}

void shell_print_status() {
	printf("Shell Status\n");
	printf("Tail %d\n", tail);
	if (shell_line != NULL) {
		printf("Line [%s]\n",shell_line);
	} else {
		printf("No data\n");
	}
}

void shell_process_start() {
	struct uploader_cfg_data cfg;

	cfg.cb_status = NULL;
	cfg.interface.init_cb = shell_process_init;
	cfg.interface.error_cb = NULL;
	cfg.interface.is_done = shell_process_is_done;
	cfg.interface.close_cb = shell_process_finish;
	cfg.interface.process_cb = shell_process_data;
	cfg.print_state = shell_print_status;

	process_set_config(&cfg);
}

#ifdef CONFIG_SHELL_UNIT_TESTS
struct shell_tests {
	char *str;
	uint32_t size;
	uint32_t result;
};

#define TEST_PARAMS(str, size, res) { str, size, res }

const struct shell_tests test[] =
{
	TEST_PARAMS("test1 ( )", 10, 3),
	TEST_PARAMS("hello world", 12, 2),
	TEST_PARAMS("h  w", 5, 2),
	TEST_PARAMS("hello", 6, 1),
	TEST_PARAMS("test2 ( ) ", 8, 2), // Cut the string
	TEST_PARAMS("test3 ", 7, 1),
	TEST_PARAMS(" test4", 7, 1),
	TEST_PARAMS(" ", 2, 0),
	TEST_PARAMS("     ", 6, 0),
	TEST_PARAMS(" ", 0, 0), // Wrong string length
	TEST_PARAMS(NULL, 0, 0)
};

void shell_unit_test() {
	uint32_t t = 0;
	uint32_t argc;

	while (t != sizeof(test) / sizeof(shell_tests)) {
		argc = shell_get_argc(test[t].str, test[t].size);
		if (argc != test[t].result) {
			printf("Failed [%s] %d!=%d ",
				test[t].str,
				test[t].result,
				argc);
			return;
		}
		t++;
	}

	char arg[32];
	uint32_t len;
	t = 0;

	while (t != sizeof(test) / sizeof(shell_tests)) {
		const char *line = test[t].str;
		argc = shell_get_argc(line, test[t].size);
		printf("Test [%s] %d\n", line, argc);
		while (argc > 0) {
			line = shell_get_next_arg(line, strlen(line), arg, &len);
			if (len != strlen(arg)) {
				printf("Failed [%s] %d!=%d ", arg, len, strlen(arg));
			}
			printf(" %d [%s]\n", test[t].result - argc, arg);
			argc--;
}
		t++;
	}

	printf("All tests were successful \n");
}
#endif