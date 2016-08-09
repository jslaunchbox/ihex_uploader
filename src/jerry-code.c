/*
* Copyright (c) Intel Corporation
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
 * @brief Simple interface to load and run javascript from our code memory stash
 *
 * Reads a program from the ROM/RAM
 */

/* Zephyr includes */
#include <zephyr.h>

#include <string.h>

/* JerryScript includes */
#include "jerry-api.h"

#include "code-memory.h"
#include "acm-shell.h"

extern void __stdout_hook_install(int(*fn)(int));

/**
*
* @brief Output one character to UART ACM
*
* @param c Character to output
* @return The character passed as input.
*/

static int acm_out(int c) {
	acm_writec((char) c);
	return 1;
}

void javascript_eval_code(const char *source_buffer) {
	jerry_value_t ret_val;

	__stdout_hook_install(acm_out);
	ret_val = jerry_eval((jerry_char_t *)source_buffer,
		strlen(source_buffer),
		false);

	if (jerry_value_has_error_flag(ret_val)) {
		printf("Failed to run JS\n");
	}

	jerry_release_value(ret_val);
}

void javascript_run_code(const char *file_name) {
	CODE *code = csopen(file_name, "r");

	if (code == NULL)
		return;

	size_t len = strlen((char *)code);
	if (len != code->curend) {
		printf("Size %u %u missmatch\n",
			(unsigned int)len,
			   (unsigned int)code->curend);
		return;
	}

	/* Setup Global scope code */
	jerry_value_t parsed_code = jerry_parse((const jerry_char_t *)code, len, false);

	if (!jerry_value_has_error_flag(parsed_code)) {
		__stdout_hook_install(acm_out);

		/* Execute the parsed source code in the Global scope */
		jerry_value_t ret_value = jerry_run(parsed_code);

		/* Returned value must be freed */
		jerry_release_value(ret_value);
	} else {
		printf("JerryScript: could not parse javascript\n");
		return;
	}

	/* Parsed source code must be freed */
	jerry_release_value(parsed_code);

	/* Cleanup engine */
	jerry_cleanup();

	/* Initialize engine */
	jerry_init(JERRY_INIT_EMPTY);

	csclose(code);
}

void javascript_run_snapshot(const char *file_name) {

}