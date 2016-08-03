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

#ifndef CONFIG_IHEX_UPLOADER_DEBUG
#define DBG(...) { ; }
#else
#if defined(CONFIG_STDOUT_CONSOLE)
#include <stdio.h>
#define DBG printf
#else
#include <misc/printk.h>
#define DBG printk
#endif /* CONFIG_STDOUT_CONSOLE */
#endif /* CONFIG_IHEX_UPLOADER_DEBUG */

const char banner[] = "Jerry Uploader " __DATE__ " " __TIME__ "\r\n";
const char filename[] = "jerry.js";

// Jerryscript in green color

const char system_prompt[] = ANSI_FG_GREEN "js> " ANSI_FG_RESTORE;
const char *system_get_prompt() {
	return system_prompt;
}

#define MAX_LINE_LEN 16
#define FIFO_CACHE 2

/* Configuration of the callbacks to be called */
static struct uploader_cfg_data uploader_config = {
	.filename = filename,
	/** Callback to be notified on connection status change */
	.cb_status = NULL,
	.interface = {
		.init_cb = NULL,
		.close_cb = NULL,
		.process_cb = NULL,
		.error_cb = NULL,
		.is_done = NULL
	},
	.print_state = NULL
};

struct uart_uploader_input {
	int _unused;
	char line[MAX_LINE_LEN + 1];
};

static struct nano_fifo avail_queue;
static struct nano_fifo data_queue;
static bool uart_process_done = false;
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
	UART_CLOSE,
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

		/* We allocate a new buffer everytime we don't have a tail
		 * the buffer might be recycled or not from a previous run.
		 */
		if (tail == 0) {
			DBG("[New]\n");
			data = fifo_get_isr_buffer();
			buf = data->line;
		}

		/* Read only until the end of the buffer
		 * before i was using a ring buffer but was making things really
		 * complicated for process side.
		 */
		len = MAX_LINE_LEN - tail;
		bytes_read = uart_fifo_read(dev_upload, buf, len);
		bytes_received += bytes_read;
		tail += bytes_read;

		data->line[tail] = 0;
		//DGB("[%s]\r\n", data->line);

		/* We don't want to flush data too fast otherwise we would be allocating
		 * but we want to flush as soon as we have processed the data on the task
		 * so we don't queue too much and delay the system response.
		 *
		 * When the process has finished dealing with the data it signals this method
		 * with a 'i am ready to continue' by changing uart_process_done.
		 *
		 * It is also imperative to flush when we reach the limit of the buffer.
		 */
		bool flush = false;

		if (uart_process_done ||
			tail == MAX_LINE_LEN ||
			(!uart_irq_rx_ready(dev) && bytes_read > 4)) {
			flush = true;
			uart_process_done = false;
		}

		/* Check for line ends, to flush the data. The decoder / shell will probably
		 * sit for a bit in the data so it is better if we finish this buffer and send it.
		 */
		uart_state = UART_FIFO_READ;
		while (bytes_read-- > 0) {
			byte = *buf++;
			if (byte == '\r' || byte == '\n')
				flush = true;
		}

		uart_state = UART_FIFO_READ_END;

		/* Happy to flush the data into the queue for processing */
		if (flush) {
			data->line[tail] = 0;
			uart_state = UART_FIFO_READ_FLUSH;
			nano_isr_fifo_put(&data_queue, data);
			data = NULL;
			tail = 0;
		}
	}
}

/*************************** ACM OUTPUT *******************************/
/*
 * Write into the data buffer,
 * really dislike this wait here from the example
 * will probably rewrite it later with a line queue
 */

void acm_write(const char *buf, int len) {
	struct device *dev = dev_upload;
	uart_irq_tx_enable(dev);

	data_transmitted = false;
	uart_fifo_fill(dev, buf, len);
	while (data_transmitted == false)
		;
	uart_irq_tx_disable(dev);
}

void acm_writec(char byte) {
	acm_write(&byte, 1);
}

void acm_print(const char *buf) {
	acm_write(buf, strlen(buf));
}

void acm_println(const char *buf) {
	acm_write(buf, strlen(buf));
	acm_write("\r\n", 3);
}

/**************************** DEVICE **********************************/

void process_set_config(struct uploader_cfg_data *config) {
	memcpy(&uploader_config, config, sizeof(struct uploader_cfg_data));
}

uint8_t uart_get_last_state() {
	return uart_state;
}

void uart_print_status() {
	printf("******* SYSTEM STATE ********\n");

	if (uploader_config.print_state != NULL)
		uploader_config.print_state();

	printf("[State] %d\n", (int)uart_get_last_state());
	printf("[Mem] Fifo %d Max Fifo %d Alloc %d Free %d \n",
		(int)fifo_size, (int)max_fifo_size, (int)alloc_count, (int)free_count);
	printf("Max fifo %d bytes\n", (int)(max_fifo_size * sizeof(struct uart_uploader_input)));
	printf("[Data] Received %d Processed %d \n",
		(int)bytes_received, (int)bytes_processed);
}

void uart_uploader_runner(int arg1, int arg2) {
	static struct uart_uploader_input *data = NULL;
	char *buf = NULL;
	uint32_t len = 0;

	DBG("[Listening]\n");
	while (1) {
		uart_state = UART_INIT;
		if (uploader_config.interface.init_cb != NULL) {
			DBG("[Init]\n");
			uploader_config.interface.init_cb(uploader_config.filename);
		}

		while (!uploader_config.interface.is_done()) {
			uart_state = UART_WAITING;

			while (data == NULL) {
				DBG("[Wait]\n");
				data = nano_task_fifo_get(&data_queue, TICKS_UNLIMITED);
				buf = data->line;
				len = strlen(buf);

				DBG("[Data]\n");
				DBG("%s\n", buf);
			}

			bytes_processed += uploader_config.interface.process_cb(buf, len);
			uart_process_done = true;

			DBG("[Recycle]\n");
			fifo_recycle_buffer(data);
			data = NULL;
		}

		uart_state = UART_CLOSE;
		if (uploader_config.interface.close_cb != NULL)
			uploader_config.interface.close_cb();
	}

	// Not possible
	uart_state = UART_TERMINATED;
}

#ifdef CONFIG_UART_LINE_CTRL
uint32_t uart_get_baudrate(void) {
	uint32_t baudrate;

	int ret = uart_line_ctrl_get(dev_upload, LINE_CTRL_BAUD_RATE, &baudrate);
	if (ret)
		printf("Fail baudrate %d\n", ret);
	else
		printf("Baudrate %d\n", (int)baudrate);

	return baudrate;
}
#endif

/* ACM TASK */
void acm() {
	dev_upload = device_get_binding(CONFIG_CDC_ACM_PORT_NAME);

	if (!dev_upload) {
		printf("CDC [%s] ACM device not found\n", CONFIG_CDC_ACM_PORT_NAME);
		return;
	}

	nano_fifo_init(&data_queue);
	nano_fifo_init(&avail_queue);

#ifdef CONFIG_UART_LINE_CTRL
	uint32_t dtr = 0;
	int ret;

	printf("Wait for DTR\n");
	while (1) {
		uart_line_ctrl_get(dev_upload, LINE_CTRL_DTR, &dtr);
		if (dtr)
			break;
	}

	/* They are optional, we use them to test the interrupt endpoint */
	ret = uart_line_ctrl_set(dev_upload, LINE_CTRL_DCD, 1);
	if (ret)
		printf("DCD Failed %d\n", ret);

	ret = uart_line_ctrl_set(dev_upload, LINE_CTRL_DSR, 1);
	if (ret)
		printf("DSR Failed %d\n", ret);

	/* Wait 1 sec for the host to do all settings */
	printf("Start\n");
	sys_thread_busy_wait(1000000);

	uart_get_baudrate();
#endif

	uart_irq_rx_disable(dev_upload);
	uart_irq_tx_disable(dev_upload);

	uart_irq_callback_set(dev_upload, interrupt_handler);
	acm_write(banner, strlen(banner));

	/* Enable rx interrupts */
	uart_irq_rx_enable(dev_upload);

	uart_uploader_runner(0, 0);
}
