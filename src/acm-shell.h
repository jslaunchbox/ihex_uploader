/* Copyright 2015 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef __ACM_SHELL__H_
#define __ACM_SHELL__H_

#ifdef __cplusplus
extern "C" {
#endif

void acm_set_prompt(const char *prompt);
const char *acm_get_prompt();

/**
 * Callback function when a line arrives
 */
typedef int32_t(*ashell_line_parser_t)(const char *buf, uint32_t len);

void ashell_process_start();
void ashell_process_close();

void ashell_register_app_line_handler(ashell_line_parser_t cb);

uint32_t ashell_get_argc(const char *str, uint32_t nsize);
const char *ashell_get_next_arg_s(const char *str, uint32_t nsize, char *str_arg, uint32_t max_arg_size, uint32_t *length);

#ifdef __cplusplus
}
#endif

#endif
