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

#ifndef __SHELL__STATE__H__
#define __SHELL__STATE__H__

 /*
 * @brief State of the shell to control the data flow
 * and transactions.
 *
 */
#define MAX_NAME_SIZE 16

enum {
	kShellTransferRaw = (1 << 0),
	kShellTransferIhex = (1 << 1),
	kShellTransferSnapshot = (1 << 2)
};

struct shell_state_config {
	/** Filename where we will be storing data */
	char filename[MAX_NAME_SIZE];
	uint32_t state_flags;
};

int32_t ashell_main_state(const char *buf, uint32_t len);

#endif
