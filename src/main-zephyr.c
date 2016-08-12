/* Copyright 2016 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <zephyr.h>
#include <misc/shell.h>

#include "jerry-api.h"

#include "uart-uploader.h"
#include "ihex-handler.h"
#include "acm-shell.h"

#define CONFIG_USE_JS_SHELL
#define CONFIG_USE_IHEX_UPLOADER

 //#define CONFIG_USE_IHEX_LOADER_ONLY

  /**
   * Jerryscript simple test loop
   */
int jerryscript_test() {
	jerry_value_t ret_val;

	const char script[] =
		"var test=0; " \
		"for (var t=100; t<1000; t++) test+=t; " \
		"print ('Hi JS World! '+test);";

	printf("Script [%s]\n", script);
	ret_val = jerry_eval((jerry_char_t *)script,
						 strlen(script),
						 false);

	return jerry_value_has_error_flag(ret_val) ? -1 : 0;
}

#ifdef CONFIG_USE_JS_SHELL
#define VERBOSE 0x01

static char *source_buffer = NULL;
static unsigned char flags = 0;

static int shell_cmd_verbose(int argc, char *argv[]) {
	printf("Enable verbose \n");
	flags |= VERBOSE;
	return 0;
}

static int shell_cmd_syntax_help(int argc, char *argv[]) {
	printf("version jerryscript & zephyr versions\n");
	return 0;
}

static int shell_cmd_version(int argc, char *argv[]) {
	uint32_t version = sys_kernel_version_get();

	printf("Jerryscript API %d.%d\n", JERRY_API_MAJOR_VERSION, JERRY_API_MINOR_VERSION);

	printf("Zephyr version %d.%d.%d\n", (int)SYS_KERNEL_VER_MAJOR(version),
		(int)SYS_KERNEL_VER_MINOR(version),
		   (int)SYS_KERNEL_VER_PATCHLEVEL(version));
	return 0;
}

static int shell_acm_command(int argc, char *argv[]) {
	if (argc <= 1)
		return -1;

	char *cmd = argv[1];

	printf("[ACM] %s\n", cmd);

#ifdef CONFIG_UART_LINE_CTRL
	if (!strcmp(cmd, "get_baudrate")) {
		uart_get_baudrate();
		return 0;
	}
#endif

	if (!strcmp(cmd, "clear")) {
		uart_clear();
		return 0;
	}

	if (!strcmp(cmd, "print")) {
		for (int t = 2; t < argc; t++) {
			if (t > 2)
				acm_write(" ", 2);
			acm_write(argv[t], strlen(argv[t]));
		}
		acm_write("\r\n", 3);
		return 0;
	}

	if (!strcmp(cmd, "status")) {
		uart_print_status();
		return 0;
	}
	printf("Command unknown\n");
	return 0;
} /* shell_acm_command */

static int shell_clear_command(int argc, char *argv[]) {
	printf(ANSI_CLEAR);
	fflush(stdout);
	return 0;
}

static int shell_cmd_test(int argc, char *argv[]) {
#ifdef TEST_SIZES
	printf("[time_t] %lu\n", sizeof(time_t));
	printf("[BYTE] %lu\n", sizeof(BYTE));
	printf("[DWORD] %lu\n", sizeof(DWORD));
	printf("[UINT] %lu\n", sizeof(UINT));

	printf("[uint8_t] %lu\n", sizeof(uint8_t));
	printf("[uint16_t] %lu\n", sizeof(uint16_t));
	printf("[uint32_t] %lu\n", sizeof(uint32_t));
	printf("[uint64_t] %lu\n", sizeof(uint64_t));

	printf("[CHAR] %lu\n", sizeof(char));
	printf("[INT] %lu\n", sizeof(int));
	printf("[LONG] %lu\n", sizeof(long));
	printf("[LONG INT] %lu\n", sizeof(long int));
	printf("[LONG LONG] %lu\n", sizeof(long long));
#endif
	return jerryscript_test();
} /* shell_cmd_test */

static int shell_cmd_handler(int argc, char *argv[]) {
	if (argc <= 0) {
		return -1;
	}

	unsigned int size = 0;
	for (int t = 0; t < argc; t++) {
		size += strlen(argv[t]) + 1;
	}

	source_buffer = (char *)malloc(size);

	char *d = source_buffer;
	unsigned int len;

	for (int t = 0; t < argc; t++) {
		len = strlen(argv[t]);
		memcpy(d, argv[t], len);
		d += len;
		*d = ' ';
		d++;
	}

	*(d - 1) = '\0';

	if (flags & VERBOSE) {
		printf("[%s] %lu\n", source_buffer, strlen(source_buffer));
	}

	jerry_value_t ret_val;

	ret_val = jerry_eval((jerry_char_t *)source_buffer,
						 strlen(source_buffer),
						 false);

	free(source_buffer);

	if (jerry_value_has_error_flag(ret_val)) {
		printf("Failed to run JS\n");
	}

	jerry_release_value(ret_val);

	return 0;
} /* shell_cmd_handler */

#define SHELL_COMMAND(name,cmd) { name, cmd }

const struct shell_cmd commands[] =
{
  SHELL_COMMAND("clear", shell_clear_command),
  SHELL_COMMAND("syntax", shell_cmd_syntax_help),
  SHELL_COMMAND("version", shell_cmd_version),
  SHELL_COMMAND("test", shell_cmd_test),
  SHELL_COMMAND("acm", shell_acm_command),
  SHELL_COMMAND("verbose", shell_cmd_verbose),

  SHELL_COMMAND(NULL, NULL)
};
#endif

void main(void) {
#ifdef CONFIG_USE_JS_SHELL
	jerry_init(JERRY_INIT_EMPTY);
	shell_clear_command(0, 0);
	printf("Jerry Shell " __DATE__ " " __TIME__ "\n");
	shell_register_app_cmd_handler(shell_cmd_handler);
	shell_init(system_get_prompt(), commands);
	/* Don't call jerry_cleanup() here, as shell_init() returns after setting
	   up background task to process shell input, and that task calls
	   shell_cmd_handler(), etc. as callbacks. This processing happens in
	   the infinite loop, so JerryScript doesn't need to be de-initialized. */
#endif

#ifdef CONFIG_USE_IHEX_LOADER_ONLY
	ihex_process_start();
#else
	ashell_process_start();
#endif
}

