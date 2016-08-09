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
#include "code-memory.h"
#include "acm-shell.h"
#include "shell-state.h"
#include "ihex-handler.h"

static const char acm_default_prompt[] = ANSI_FG_YELLOW "acm> " ANSI_FG_RESTORE;
static const char *acm_prompt = NULL;
void acm_set_prompt(const char *prompt) {
	acm_prompt = prompt;
}

const char *acm_get_prompt() {
	if (acm_prompt == NULL)
		return acm_default_prompt;
	return acm_prompt;
}

//#define CONFIG_SHELL_UPLOADER_DEBUG

#ifndef CONFIG_SHELL_UPLOADER_DEBUG
#define DBG(...) { ; }
#else
#if defined(CONFIG_STDOUT_CONSOLE)
#define DBG printf
#else
#define DBG printk
#endif /* CONFIG_STDOUT_CONSOLE */
#endif /* CONFIG_SHELL_UPLOADER_DEBUG */

#define MAX_LINE 64
#define MAX_ARGUMENT_SIZE 32

static ashell_line_parser_t app_line_cb = NULL;
static char *shell_line = NULL;
static uint8_t tail = 0;
static bool ashell_is_done = false;

static inline void cursor_forward(unsigned int count) {
	for (int t = 0; t < count; t++)
		acm_print("\x1b[1C");
}

static inline void cursor_backward(unsigned int count) {
	for (int t = 0; t < count; t++)
		acm_print("\x1b[1D");
}

static inline void cursor_save(void) {
	acm_print("\x1b[s");
}

static inline void cursor_restore(void) {
	acm_print("\x1b[u");
}

static void insert_char(char *pos, char c, uint8_t end) {
	char tmp;

	/* Echo back to console */
	acm_writec(c);

	if (end == 0) {
		*pos = c;
		return;
	}

	tmp = *pos;
	*(pos++) = c;

	cursor_save();

	while (end-- > 0) {
		acm_writec(tmp);
		c = *pos;
		*(pos++) = tmp;
		tmp = c;
	}

	/* Move cursor back to right place */
	cursor_restore();
}

static void del_char(char *pos, uint8_t end) {
	acm_writec('\b');

	if (end == 0) {
		acm_writec(' ');
		acm_writec('\b');
		return;
	}

	cursor_save();

	while (end-- > 0) {
		*pos = *(pos + 1);
		acm_writec(*(pos++));
	}

	acm_writec(' ');

	/* Move cursor back to right place */
	cursor_restore();
}

enum {
	ESC_ESC,
	ESC_ANSI,
	ESC_ANSI_FIRST,
	ESC_ANSI_VAL,
	ESC_ANSI_VAL_2
};

static atomic_t esc_state;
static unsigned int ansi_val, ansi_val_2;
static uint8_t cur, end;

static void handle_ansi(uint8_t byte) {
	if (atomic_test_and_clear_bit(&esc_state, ESC_ANSI_FIRST)) {
		if (!isdigit(byte)) {
			ansi_val = 1;
			goto ansi_cmd;
		}

		atomic_set_bit(&esc_state, ESC_ANSI_VAL);
		ansi_val = byte - '0';
		ansi_val_2 = 0;
		return;
	}

	if (atomic_test_bit(&esc_state, ESC_ANSI_VAL)) {
		if (isdigit(byte)) {
			if (atomic_test_bit(&esc_state, ESC_ANSI_VAL_2)) {
				ansi_val_2 *= 10;
				ansi_val_2 += byte - '0';
			} else {
				ansi_val *= 10;
				ansi_val += byte - '0';
			}
			return;
		}

		/* Multi value sequence, e.g. Esc[Line;ColumnH */
		if (byte == ';' &&
			!atomic_test_and_set_bit(&esc_state, ESC_ANSI_VAL_2)) {
			return;
		}

		atomic_clear_bit(&esc_state, ESC_ANSI_VAL);
		atomic_clear_bit(&esc_state, ESC_ANSI_VAL_2);
	}

ansi_cmd:
	switch (byte) {
		case ANSI_BACKWARD:
			if (ansi_val > cur) {
				break;
			}

			end += ansi_val;
			cur -= ansi_val;
			cursor_backward(ansi_val);
			break;
		case ANSI_FORWARD:
			if (ansi_val > end) {
				break;
			}

			end -= ansi_val;
			cur += ansi_val;
			cursor_forward(ansi_val);
			break;
		default:
			break;
	}

	atomic_clear_bit(&esc_state, ESC_ANSI);
}

/** @brief Get the number of arguments in a string
 *
 * @param str   Null terminated string
 * @param nsize Check for size boundaries
 * @return Returns the number of arguments on the string
 */

uint32_t ashell_get_argc(const char *str, uint32_t nsize) {
	if (str == NULL || nsize == 0 || *str == '\0')
		return 0;

	uint32_t size = 1;
	bool div = false;

	/* Skip the first spaces */
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

/** @brief Copy the next argument into the string
 *
 * @param str     Null terminated string
 * @param nsize   Checks line size boundaries.
 * @param str_arg Initialized destination for the argument
 * @param length  Returns length of the argument found.
 * @return 0 Pointer to where this argument finishes
 */

const char *ashell_get_next_arg(const char *str, uint32_t nsize, char *str_arg, uint32_t *length) {
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

uint32_t ashell_process_init(const char *filename) {
	printf("[SHELL] Init\n");
	acm_set_prompt(acm_default_prompt);
	acm_println("");
	acm_print(acm_get_prompt());
	return 0;
}

void ashell_process_line(const char *buf, uint32_t len) {
#ifdef CONFIG_SHELL_UPLOADER_DEBUG
	char arg[MAX_ARGUMENT_SIZE];
	uint32_t argc, arg_len;

	printk("[BOF]");
	printk("%s", buf);
	printk("[EOF]\n");
	argc = ashell_get_argc(buf, len);

	printk("[ARGS %u]\n", argc);
	for (int t = 0; t < argc; t++) {
		buf = ashell_get_next_arg(buf, len, arg, &arg_len);
		len -= arg_len;
		printf(" Arg [%s]::%d \n", arg, (int)arg_len);
	}
#endif
	printk(system_get_prompt());
	acm_print(acm_get_prompt());
}

uint32_t ashell_process_data(const char *buf, uint32_t len) {
	uint32_t processed = 0;
	bool flush_line = false;

	if (shell_line == NULL) {
		DBG("[Proccess]%d\n", (int)len);
		DBG("[%s]\n", buf);
		shell_line = (char *)malloc(MAX_LINE);
		memset(shell_line, 0, MAX_LINE);
		tail = 0;
	}

	while (len-- > 0) {
		processed++;
		uint8_t byte = *buf++;

		if (tail == MAX_LINE) {
			DBG("Line size exceeded \n");
			tail = 0;
		}

		DBG("(%x)", byte);

		/* Handle ANSI escape mode */
		if (atomic_test_bit(&esc_state, ESC_ANSI)) {
			handle_ansi(byte);
			continue;
		}

		/* Handle escape mode */
		if (atomic_test_and_clear_bit(&esc_state, ESC_ESC)) {
			switch (byte) {
				case ANSI_ESC:
					atomic_set_bit(&esc_state, ESC_ANSI);
					atomic_set_bit(&esc_state, ESC_ANSI_FIRST);
					break;
				default:
					break;
			}

			continue;
		}

		/* Handle special control characters */
		if (!isprint(byte)) {
			switch (byte) {
				case ASCII_DEL:
					if (cur > 0) {
						del_char(&shell_line[--cur], end);
					}
					break;
				case ASCII_ESC:
					atomic_set_bit(&esc_state, ESC_ESC);
					break;
				case ASCII_CR:
					DBG("<CR>\n");
					flush_line = true;
					break;
				case ASCII_TAB:
					acm_writec('\t');
					break;
				case ASCII_IF:
					DBG("<IF>");
					break;
				default:
					printf("<CTRL> %u\n", byte);
					flush_line = true;
					shell_line[cur++] = byte;
					break;
			}
		}

		if (flush_line) {
			DBG("Line %u %u \n", cur, end);
			shell_line[cur + end] = '\0';
			acm_write("\r\n", 3);

			uint32_t length = strlen(shell_line);
			int32_t ret = 0;
			if (app_line_cb != NULL)
				ret = app_line_cb(shell_line, length);

			if (ret <= 0)
				ashell_process_line(shell_line, length);

			cur = end = 0;
			flush_line = false;
		} else
			if (isprint(byte)) {
				/* Ignore characters if there's no more buffer space */
				if (cur + end < MAX_LINE - 1) {
					insert_char(&shell_line[cur++], byte, end);
				} else {
					printf("Max line\n");
				}
			}

	}

	/* Done processing line */
	if (cur == 0 && end == 0 && shell_line != NULL) {
		DBG("[Free]\n");
		free(shell_line);
		shell_line = NULL;
	}
	return processed;
}

bool ashell_process_is_done() {
	if (ashell_is_done) {
		printf("[Done]\n");
	}
	return ashell_is_done;
}

uint32_t ashell_process_finish() {
	printf("[SHELL CLOSE]\n");
	ihex_process_start();
	return 0;
}

void ashell_print_status() {
	printf("Shell Status\n");
	printf("Tail %d\n", tail);
	if (shell_line != NULL) {
		printf("Line [%s]\n", shell_line);
	} else {
		printf("No data\n");
	}
}

void ashell_register_app_line_handler(ashell_line_parser_t cb) {
	app_line_cb = cb;
}

void ashell_process_close() {
	ashell_is_done = true;
}

void ashell_process_start() {
	struct uploader_cfg_data cfg;

	ashell_is_done = false;
	cfg.cb_status = NULL;
	cfg.interface.init_cb = ashell_process_init;
	cfg.interface.error_cb = NULL;
	cfg.interface.is_done = ashell_process_is_done;
	cfg.interface.close_cb = ashell_process_finish;
	cfg.interface.process_cb = ashell_process_data;
	cfg.print_state = ashell_print_status;

	process_set_config(&cfg);

	ashell_register_app_line_handler(ashell_main_state);
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
	TEST_PARAMS("test2 ( ) ", 8, 2), /* Cut the string */
	TEST_PARAMS("test3 ", 7, 1),
	TEST_PARAMS(" test4", 7, 1),
	TEST_PARAMS(" ", 2, 0),
	TEST_PARAMS("     ", 6, 0),
	TEST_PARAMS(" ", 0, 0), /* Wrong string length */
	TEST_PARAMS(NULL, 0, 0)
};

void shell_unit_test() {
	uint32_t t = 0;
	uint32_t argc;

	while (t != sizeof(test) / sizeof(shell_tests)) {
		argc = ashell_get_argc(test[t].str, test[t].size);
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
		argc = ashell_get_argc(line, test[t].size);
		printf("Test [%s] %d\n", line, argc);
		while (argc > 0) {
			line = ashell_get_next_arg(line, strlen(line), arg, &len);
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