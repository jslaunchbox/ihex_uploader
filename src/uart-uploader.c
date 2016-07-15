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
* @brief UART-driven uploader
*
* Reads a program from the uart using Intel HEX format.
*
* Designed to be used from Javascript or a ECMAScript object file.
*
* Hooks into the printk and fputc (for printf) modules. Poll driven.
*/

#include <nanokernel.h>
#include <arch/cpu.h>

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <ctype.h>

#include <device.h>
#include <init.h>

#include <board.h>
#include <uart.h>
#include <toolchain.h>
#include <sections.h>
#include <atomic.h>
#include <misc/printk.h>

#include "code-memory.h"
#include "jerry-code.h"

#include "uart-uploader.h"
#include "ihex/kk_ihex_read.h"

#define CONFIG_UART_UPLOAD_HANDLER_STACKSIZE 2000

#define STACKSIZE CONFIG_UART_UPLOAD_HANDLER_STACKSIZE

const char banner[] = "Jerry Uploader " __DATE__ " " __TIME__ "\n";

//#define DEBUG_UART

/*
 * Contains the pointer to the memory where the code will be uploaded
 * using the stub interface at code_memory.c
 */
static CODE *code_memory = NULL;

/********************** DATA PROCESS ****************************************/

/* Control characters for testing and debugging */
#define ESC                0x1b
#define DEL                0x7f

/****************************** IHEX ****************************************/

static struct ihex_state ihex;

static int8_t upload_state = 0;
#define UPLOAD_START       0
#define UPLOAD_IN_PROGRESS 1
#define UPLOAD_FINISHED    2
#define UPLOAD_ERROR       -1

/* Data received from the buffer */
ihex_bool_t ihex_data_read(struct ihex_state *ihex,
	ihex_record_type_t type,
	ihex_bool_t checksum_error) {

	if (checksum_error) {
		upload_state = UPLOAD_ERROR;
		return false;
	};

	if (type == IHEX_DATA_RECORD) {
		upload_state = UPLOAD_IN_PROGRESS;
		unsigned long address = (unsigned long)IHEX_LINEAR_ADDRESS(ihex);
		ihex->data[ihex->length] = 0;
#ifdef DEBUG_UART
		printf("%d::%d:: %s \n", (int)address, ihex->length, ihex->data);
#endif
		csseek(code_memory, address, SEEK_SET);
		cswrite(ihex->data, ihex->length, 1, code_memory);
	}
	else if (type == IHEX_END_OF_FILE_RECORD) {
		print_acm("[EOF]\n");
		upload_state = UPLOAD_FINISHED;
	}
	return true;
}

/**************************** UART CAPTURE **********************************/

static struct device *dev_upload;

static volatile bool data_transmitted;
static volatile bool data_arrived;
static char data_buf[128];

static void interrupt_handler(struct device *dev)
{
	uart_irq_update(dev);

	if (uart_irq_tx_ready(dev)) {
		data_transmitted = true;
	}

	if (uart_irq_rx_ready(dev)) {
		data_arrived = true;
	}
}

static void write_data(struct device *dev, const char *buf, int len)
{
	uart_irq_tx_enable(dev);

	data_transmitted = false;
	uart_fifo_fill(dev, buf, len);
	while (data_transmitted == false);

	uart_irq_tx_disable(dev);
}

void print_acm(const char *buf)
{
	write_data(dev_upload, buf, strlen(buf));
}

/*
static void read_and_echo_data(struct device *dev, int *bytes_read)
{
	while (data_arrived == false)
		;

	data_arrived = false;

	while ((*bytes_read = uart_fifo_read(dev,
		data_buf, sizeof(data_buf)))) {
		write_data(dev, data_buf, *bytes_read);
	}
}
*/

/**************************** DEVICE **********************************/
/*
* Negotiate a re-upload
*/

void uart_handle_upload_error() {
	printf("[Download Error]\n");
}

void uart_uploader_runner()
{
	uint32_t bytes_read = 0;
	uint32_t pos = 0;
	bool begin_marker = false;
	const char *code_name = "test.js";

	while (1) {
		upload_state = UPLOAD_START;
		print_acm("[Waiting for data]\n");

		ihex_begin_read(&ihex);
		code_memory = csopen(code_name, "w+");

		while (upload_state != UPLOAD_FINISHED) {
			while (data_arrived == false);
			data_arrived = false;

			/* Read all data and echo it back */
			bytes_read = uart_fifo_read(dev_upload, &data_buf[pos], sizeof(data_buf) - pos);
			//write_data(dev_upload, &data_buf[pos], bytes_read);

			for (int t = 0; t < bytes_read; t++) {
				uint8_t byte = data_buf[pos++];
				write_data(dev_upload, &byte, 1);
				switch(byte) {
					case ':':
						begin_marker = true;
						pos = 0;
						printf("[BGN]\n");
					break;
					case '\r':
						printf("[RTN]\n");
						data_buf[pos] = 0;
						if (begin_marker) {
							begin_marker = false;
							ihex_read_bytes(&ihex, data_buf, strlen(data_buf));
						}

						data_buf[pos - 1] = 0;
						printf("[Read][%d][%s]\n", (int) pos, data_buf);
						pos = 0;
						break;
					case '\n':
						printf("[EOL]\n");
					break;
				}
			}
			if (upload_state == UPLOAD_ERROR) {
				uart_handle_upload_error();
				break;
			}
		}

		if (upload_state == UPLOAD_FINISHED) {
			csclose(code_memory);
			ihex_end_read(&ihex);
			javascript_run_code(code_name);
		}

	}
}

void uart_rx_renable(void) {
	uart_irq_rx_enable(dev_upload);
}

void uart_uploader_init(void) {
	uint32_t baudrate, dtr = 0;
	int ret;

	dev_upload = device_get_binding(CONFIG_CDC_ACM_PORT_NAME);
	if (!dev_upload) {
		printf("CDC ACM device not found\n");
		return;
	}

	printf("Wait for DTR\n");
	while (1) {
		uart_line_ctrl_get(dev_upload, LINE_CTRL_DTR, &dtr);
		if (dtr)
			break;
	}
	printf("DTR set, start test\n");

	/* They are optional, we use them to test the interrupt endpoint */
	ret = uart_line_ctrl_set(dev_upload, LINE_CTRL_DCD, 1);
	if (ret)
		printf("Failed to set DCD, ret code %d\n", ret);

	ret = uart_line_ctrl_set(dev_upload, LINE_CTRL_DSR, 1);
	if (ret)
		printf("Failed to set DSR, ret code %d\n", ret);

	/* Wait 1 sec for the host to do all settings */
	sys_thread_busy_wait(1000000);

	ret = uart_line_ctrl_get(dev_upload, LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret)
		printf("Failed to get baudrate, ret code %d\n", ret);
	else
		printf("Baudrate detected: %d\n", (int)baudrate);

	uart_irq_rx_disable(dev_upload);
	uart_irq_tx_disable(dev_upload);

	uart_irq_callback_set(dev_upload, interrupt_handler);
	write_data(dev_upload, banner, strlen(banner));

	/* Enable rx interrupts */
	uart_irq_rx_enable(dev_upload);

	uart_uploader_runner();
}

