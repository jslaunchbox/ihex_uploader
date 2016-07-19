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
#include <malloc.h>
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

#define MAX_LINE_LEN 16
#define FIFO_CACHE 3
struct uart_uploader_input {
	int _unused;
	char line[MAX_LINE_LEN + 1];
};

static struct nano_fifo avail_queue;
static struct nano_fifo data_queue;
static uint8_t fifo_size = 0;
static uint8_t max_fifo_size = 0;

uint32_t alloc_count = 0;
uint32_t free_count = 0;

struct uart_uploader_input *fifo_get_isr_buffer() {
	void *data = nano_isr_fifo_get(&avail_queue, TICKS_NONE);
	if (!data) {
		data = (void *)malloc(sizeof(struct uart_uploader_input));
		memset(data, '-', sizeof(struct uart_uploader_input));
		alloc_count++;
		fifo_size++;
		if (fifo_size > max_fifo_size)
			max_fifo_size = fifo_size;
	}
	return (struct uart_uploader_input *) data;
}

void fifo_recycle_buffer(struct uart_uploader_input *data) {
	if (fifo_size > FIFO_CACHE) {
		free(data);
		fifo_size--;
		free_count++;
		return;
	}
	nano_task_fifo_put(&avail_queue, data);
}

void uart_clear(void) {
	void *data = NULL;
	do {
		if (data != NULL)
			free(data);
		data = nano_fifo_get(&avail_queue, TICKS_NONE);
	} while (data);

	do {
		if (data != NULL)
			free(data);
		data = nano_fifo_get(&data_queue, TICKS_NONE);
	} while (data);
}

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

static bool marker = false;

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

uint32_t bytes_received = 0;
uint32_t bytes_processed = 0;

uint8_t uart_state = 0;
enum {
	UART_INIT,
	UART_TX_READY,
	UART_IRQ_UPDATE,
	UART_FIFO_WAIT,
	UART_RX_READY,
	UART_FIFO_READ,
	UART_FIFO_READ_END,
	UART_FIFO_READ_FLUSH,
	UART_FIFO_DATA_PROCESS,
	UART_RESET_HEAD,
	UART_POST_RESET,
	UART_PROCESS_ENDED,
	UART_RESET_TAIL,
	UART_BUFFER_OVERFLOW,
	UART_BUFFER_PROCESS_OVERFLOW,
	UART_WAITING,
	UART_TIMEOUT,
	UART_TERMINATED
};

static struct uart_uploader_input *data = NULL;
static uint32_t tail = 0;
static char *buf;

static void interrupt_handler(struct device *dev) {
	char byte;

	uint32_t bytes_read = 0;
	uint32_t len = 0;

	uart_state = UART_IRQ_UPDATE;

	if (!uart_irq_is_pending(dev))
		return;

	if (uart_irq_tx_ready(dev)) {
		data_transmitted = true;
		uart_state = UART_TX_READY;
	}

	while (uart_irq_rx_ready(dev)) {
		uart_state = UART_RX_READY;

		if (tail == 0) {
			//printf("[New]\r\n");
			data = fifo_get_isr_buffer();
			buf = data->line;
		}

		len = MAX_LINE_LEN - tail;
		bytes_read = uart_fifo_read(dev_upload, buf, len);
		bytes_received += bytes_read;
		tail += bytes_read;

		data->line[tail] = 0;
		//printf("[%s]\r\n", data->line);

		while (bytes_read > 0) {
			uart_state = UART_FIFO_READ;
			byte = *buf;
			if (tail == MAX_LINE_LEN || byte == '\r' || byte == '\n') {
				data->line[tail] = 0;
				uart_state = UART_FIFO_READ_FLUSH;
				nano_isr_fifo_put(&data_queue, data);
				data = NULL;
				tail = 0;
				break;
			}

			buf++;
			bytes_read--;
		}

		uart_state = UART_FIFO_READ_END;
	}
}

static void write_data(const char *buf, int len) {
	struct device *dev = dev_upload;
	uart_irq_tx_enable(dev);
	data_transmitted = false;
	uart_fifo_fill(dev, buf, len);
}

void print_acm(const char *buf) {
	write_data(buf, strlen(buf));
	write_data("\r\n", 3);
}

void nprint(const char *buf, unsigned int size) {
	for (int t = 0; t < size; t++) {
		if (buf[t] == '\r')
			printf("[CR]");

		if (buf[t] == '\n')
			printf("[IF]");

		if ((buf[t] == '\r' && buf[t + 1] != '\n') || (buf[t] == '\n' && buf[t - 1] != '\r')) {
			printf("*\r\n");
		}
		else {
			printf("%c", buf[t]);
		}
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

void uart_printbuffer() {
	printf("[Mem] Fifo %d Max Fifo %d Alloc %d Free %d \n",
		(int)fifo_size, (int)max_fifo_size, (int)alloc_count, (int)free_count);
	printf("[Data] Received %d Processed %d \n",
		(int)bytes_received, (int)bytes_processed);
	if (marker)
		printf("[Marker]\n");
	printf("[START]\n");
	//uart_print_buffer_line(data_buf, 0, tail);
	printf("[END]\n");
}

void check_at_command(const char *buf, uint32_t lstart) {
	if (lstart == 0)
		return;

	if (buf[lstart] != 'T')
		return;
	if (buf[lstart - 1] != 'A')
		return;

	printf("OK\r\n");
	write_data("OK\r\n", 5);
}

void uart_uploader_runner(int arg1, int arg2) {
	static struct uart_uploader_input *data = NULL;
	char *buf = NULL;
	uint32_t len = 0;

	while (1) {
		upload_state = UPLOAD_START;
		printf("[Waiting for data]");

		ihex_begin_read(&ihex);
		code_memory = csopen(code_name, "w+");

		while (upload_state != UPLOAD_FINISHED) {
			uart_state = UART_WAITING;

			while (data == NULL) {
				//printf("[Wait]\r\n");
				data = nano_task_fifo_get(&data_queue, TICKS_UNLIMITED);
				buf = data->line;
				//printf("[Data]\r\n");
				len = strlen(buf);

				printf("%s\r\n", buf);
				// Finished transmiting, disable tx irq
				if (data_transmitted == true)
					uart_irq_tx_disable(dev_upload);
			}

			while (len-- > 0) {
				uart_state = UART_FIFO_DATA_PROCESS;
				char byte = *buf++;
				//write_data(&byte, 1);
				bytes_processed++;

				if (marker) {
					ihex_read_byte(&ihex, byte);
				}

				switch (byte) {
					case ':':
						//write_data("[MK]", 4);
						printf("<MK>");
						ihex_read_byte(&ihex, byte);
						marker = true;
						break;
					case '\r':
						if (!marker) {
							check_at_command(buf, data->line - buf);
						}
						marker = false;
						printf("<CR>");
						break;
						//write_data("[CR]", 4);
					case '\n':
						marker = false;
						printf("<IF>");
						//write_data("[IF]", 4);
						break;
				}
			}

			//printf("[Recycle]\r\n");
			fifo_recycle_buffer(data);
			data = NULL;

			if (upload_state == UPLOAD_ERROR) {
				uart_handle_upload_error();
				break;
			}
		}

		if (upload_state == UPLOAD_FINISHED) {
			printf("-----------FINISHED----------\n");
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

void acm() {
	uint32_t dtr = 0;
	int ret;

	dev_upload = device_get_binding(CONFIG_CDC_ACM_PORT_NAME);
	if (!dev_upload) {
		printf("CDC [%s] ACM device not found\n", CONFIG_CDC_ACM_PORT_NAME);
		return -1;
	}

	nano_fifo_init(&data_queue);
	nano_fifo_init(&avail_queue);

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

	uart_uploader_runner(0, 0);
	printf("Init finished\n");
}