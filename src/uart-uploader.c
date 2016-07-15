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

const char banner[] = "Jerry Uploader " __DATE__ " " __TIME__ "\r\n";
const char code_name[] = "test.js";

#define CONFIG_UART_UPLOAD_HANDLER_STACKSIZE 2000
#define STACKSIZE CONFIG_UART_UPLOAD_HANDLER_STACKSIZE
static char __stack fiberStack[STACKSIZE];
static nano_thread_id_t uploader_fiber_id = 0;

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
		printf("[ERR] Checksum_error\n");
		return false;
	};

	if (type == IHEX_DATA_RECORD) {
		upload_state = UPLOAD_IN_PROGRESS;
		unsigned long address = (unsigned long)IHEX_LINEAR_ADDRESS(ihex);
		ihex->data[ihex->length] = 0;
		//#ifdef DEBUG_UART
		printf("%d::%d:: %s \n", (int)address, ihex->length, ihex->data);
		//#endif
		csseek(code_memory, address, SEEK_SET);
		cswrite(ihex->data, ihex->length, 1, code_memory);
	}
	else if (type == IHEX_END_OF_FILE_RECORD) {
		print_acm("[EOF]");
		upload_state = UPLOAD_FINISHED;
	}
	return true;
}

/**************************** UART CAPTURE **********************************/

static struct device *dev_upload;

static volatile bool data_transmitted;
static volatile bool data_arrived;

// Ring buffer for the UART
#define RING_BUFFER_LENGTH 512
static char data_buf[RING_BUFFER_LENGTH];
uint32_t head = 0;   // buffer process head, position where i am reading
uint32_t tail = 0;   // buffer process tail, where i am writing.

uint8_t uart_state = 0;
enum {
	UART_INIT,
	UART_TX_READY,
	UART_IRQ_UPDATE,
	UART_FIFO_WAIT,
	UART_RX_READY,
	UART_FIFO_READ,
	UART_FIFO_DATA_PROCESS,
	UART_RESET_HEAD,
	UART_POST_RESET,
	UART_PROCESS_ENDED,
	UART_RESET_TAIL,
	UART_BUFFER_OVERFLOW,
	UART_BUFFER_PROCESS_OVERFLOW,
	UART_WAITING,
	UART_TERMINATED
};

static void interrupt_handler(struct device *dev)
{
	uart_state = UART_IRQ_UPDATE;
	uart_irq_update(dev);

	if (uart_irq_tx_ready(dev)) {
		data_transmitted = true;
		uart_state = UART_TX_READY;
	}

	if (uart_irq_rx_ready(dev)) {
		data_arrived = true;
		uart_state = UART_RX_READY;
		isr_fiber_wakeup(uploader_fiber_id);
	}
}

static void write_data(const char *buf, int len)
{
	struct device *dev = dev_upload;
	uart_irq_tx_enable(dev);

	data_transmitted = false;
	uart_fifo_fill(dev, buf, len);
	while (data_transmitted == false);

	uart_irq_tx_disable(dev);
}

void print_acm(const char *buf)
{
	write_data(buf, strlen(buf));
	write_data("\r\n", 3);
}

void nprint(const char *buf, unsigned int size)
{
	for (int t = 0; t < size; t++) {
		printf("%c", buf[t]);
	}
}

/**************************** DEVICE **********************************/
/*
* Negotiate a re-upload
*/

void uart_handle_upload_error() {
	printf("[Download Error]\n");
}

uint8_t uart_get_last_state() {
	return uart_state;
}

void uart_print_buffer_line(const char *data_buf, uint32_t lstart, uint32_t head) {
	if (lstart > head) {
		uint32_t len = RING_BUFFER_LENGTH - lstart;
		nprint(data_buf + lstart, len);
		nprint(data_buf, head);
	}
	else {
		nprint(data_buf + lstart, head - lstart);
	}
}

void uart_printbuffer() {
	printf("[Ring buffer] Size %d - Head %d, Tail %d \n",
		(int) RING_BUFFER_LENGTH, (int) head, (int) tail);

	printf("[START]\n");
	uart_print_buffer_line(data_buf, 0, tail);
	printf("[END]\n");
}

void uart_uploader_runner(int arg1, int arg2)
{
	uint32_t bytes_read = 0;
	uint32_t lstart = 0; // line start
	uint32_t len = 0;    // buffer available
	bool marker = false;

	while (1) {
		upload_state = UPLOAD_START;
		print_acm("[Waiting for data]");

		ihex_begin_read(&ihex);
		code_memory = csopen(code_name, "w+");

		memset(data_buf, 0, RING_BUFFER_LENGTH);

		while (upload_state != UPLOAD_FINISHED) {
			uart_state = UART_WAITING;
			while (data_arrived == false) {
				// Sleep while we don't have data
				fiber_sleep(MSEC(60000));
				if (data_arrived == false) {
					printf("[Timeout]\n");
					lstart = head = tail = 0;
				}
			};
			data_arrived = false;

			uart_state = UART_FIFO_WAIT;
			if (tail == RING_BUFFER_LENGTH) {
				uart_state = UART_RESET_TAIL;
				tail = 0;
			}
			len = sizeof(data_buf) - tail;

			bytes_read = uart_fifo_read(dev_upload, &data_buf[tail], len);
			tail += bytes_read;
			uart_state = UART_FIFO_READ;

			for (int t = 0; t < bytes_read; t++) {
				uart_state = UART_FIFO_DATA_PROCESS;
				uint8_t byte = data_buf[head];
				//write_data(&byte, 1);
				switch (byte) {
				case ':':
					lstart = head;
					marker = true;
					break;
				case '\r':
					printf("[IF]");
					uart_print_buffer_line(data_buf, lstart, head);
					printf("\n");
					lstart = head + 1;
					break;
				case '\n':
					if (marker) {
						printf("[EOM] %d - %d - %d \n", (int) lstart, (int) head, (int)tail);
						uart_print_buffer_line(data_buf, lstart, head);
						if (lstart > head) {
							ihex_read_bytes(&ihex, data_buf + lstart, RING_BUFFER_LENGTH - lstart);
							ihex_read_bytes(&ihex, data_buf, head);
						}
						else {
							ihex_read_bytes(&ihex, data_buf + lstart, head - lstart);
						}

						printf("[B]");
						uart_print_buffer_line(data_buf, lstart, head);
						printf("[E]");
						marker = false;
					} else {
						printf("[CR]");
						uart_print_buffer_line(data_buf, lstart, head);
					}

					lstart = head + 1;
					break;
				}

				head++;
				if (head == RING_BUFFER_LENGTH) {
					uart_state = UART_RESET_HEAD;
					head = 0;
				}
				uart_state = UART_POST_RESET;
			}

			uart_state = UART_PROCESS_ENDED;

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

	// Not possible
	uart_state = UART_TERMINATED;
}

void uart_rx_renable(void) {
	printf("Renable acm\n");

	dev_upload = device_get_binding(CONFIG_CDC_ACM_PORT_NAME);
	if (!dev_upload) {
		printf("CDC ACM device not found\n");
		return;
	}

	uart_get_baudrate();

	uart_irq_rx_disable(dev_upload);
	uart_irq_tx_disable(dev_upload);

	uart_irq_callback_set(dev_upload, interrupt_handler);
	write_data(banner, strlen(banner));

	/* Enable rx interrupts */
	uart_irq_rx_enable(dev_upload);
}

uint32_t uart_get_baudrate(void) {
	uint32_t baudrate;

	int ret = uart_line_ctrl_get(dev_upload, LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret)
		printf("Failed to get baudrate, ret code %d\n", ret);
	else
		printf("Baudrate detected: %d\n", (int)baudrate);

	return baudrate;
}

void uart_uploader_init(void) {
	uint32_t dtr = 0;
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

	uart_get_baudrate();

	uart_irq_rx_disable(dev_upload);
	uart_irq_tx_disable(dev_upload);

	uart_irq_callback_set(dev_upload, interrupt_handler);
	write_data(banner, strlen(banner));

	/* Enable rx interrupts */
	uart_irq_rx_enable(dev_upload);

	uploader_fiber_id = task_fiber_start(fiberStack, STACKSIZE, uart_uploader_runner, 0, 0, 3, 0);
	printf("Init finished\n");
}