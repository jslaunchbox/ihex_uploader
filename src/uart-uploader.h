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

#ifndef __UART_UPLOADER_H__
#define __UART_UPLOADER_H__

 /**
 * Ansi helpers
 * https://telepathy.freedesktop.org/doc/telepathy-glib/telepathy-glib-debug-ansi.html
 */

#define ANSI_FG_BLUE    "\x1b[34m"
#define ANSI_FG_YELLOW  "\x1b[33m"
#define ANSI_FG_GREEN   "\x1b[32m"
#define ANSI_FG_RED     "\x1b[31m"
#define ANSI_FG_RESTORE "\x1b[39m"

/**
 * Returns the command prompt of this system
 */
const char *system_get_prompt();

/**
 * Callback function initialize the process
 */
typedef uint32_t(*process_init_callback_t)(const char *filename);

/**
* Callback function to pass an error from the transmision
*/
typedef void(*process_error_callback_t)(uint32_t error);

/**
* Callback function to pass an error from the transmision
*/
typedef uint32_t(*process_data_callback_t)(const char *buf, uint32_t len);

/**
* Callback to tell when the data transfered is finished or process completed
*/
typedef bool(*process_is_done)();

/**
* Callback function to pass an error from the transmision
*/
typedef uint32_t(*process_close_callback_t)();

/* Callback to print debug data or state to the user */
typedef void(*process_print_state_t)();

/**
* Process Status Codes
*/
enum process_status_code {
	PROCESS_ERROR,        /* Error during upload */
	PROCESS_RESET,        /* Data reset */
	PROCESS_CONNECTED,    /* Client connected */
	PROCESS_UNKNOWN       /* Initial status */
};

/**
* Callback function with different status
* When a new usb device is detected or when we are ready to receive data
*/
typedef void(*process_status_callback_t)(enum process_status_code status_code);

/*
* @brief Interfaces for the different uploaders and process handlers
*/
struct uploader_interface_cfg_data {
	process_init_callback_t init_cb;
	process_close_callback_t close_cb;
	process_data_callback_t process_cb;
	process_error_callback_t error_cb;
	process_is_done is_done;
};

/*
* @brief UART process data configuration
*
* The Application instantiates this with given parameters added
* using the "process_set_config" function.
*
* This function can be called to swap between different states of the
* data transactions.
*/

struct uploader_cfg_data {
	/** Filename where we will be storing data */
	const char *filename;
	/** Callback to be notified on connection status change */
	process_status_callback_t cb_status;
	struct uploader_interface_cfg_data interface;

	/* Callback to print debug data or state to the user */
	process_print_state_t print_state;
};

void process_set_config(struct uploader_cfg_data *config);

uint32_t uart_get_baudrate(void);
uint8_t uart_get_last_state();
void uart_print_status();
void uart_clear();

void acm_println(const char *buf);
void acm_write(const char *buf, int len);
void acm_writec(char byte);
void acm_print(const char *buf);

#endif
