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
 * @brief Simulates the disk access to create a writtable memory section
 * to help on the transactions between the UART and the Javascript code
 * this is a basic stub, do not expect a full implementation.
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

#include "code-memory.h"

struct code_memory memory_code = {
	.filename = "empty.txt",
	.curoff = 0,
	.curend = 0,
	.maxsize = MAX_JAVASCRIPT_CODE_LEN
};

// Save into flash
/*
int qm_flash_page_write(const qm_flash_t flash, const qm_flash_region_t region,
	uint32_t page_num, const uint32_t *const data,
	uint32_t len)
*/

CODE *csopen(const char * filename, const char * mode) {
	printf("[OPEN FILE]\n");
	memory_code.curoff = 0;
	strncpy(memory_code.filename, filename, MAX_NAME_SIZE);
	csdescribe(&memory_code);
	return &memory_code;
}

int csseek(CODE *stream, long int offset, int whence) {
	switch (whence) {
	case SEEK_CUR:
		stream->curoff += offset;
		break;
	case SEEK_SET:
		stream->curoff = offset;
		break;
	case SEEK_END:
		stream->curoff = stream->curend + offset;
		break;
	default:
		return (EOF);
	}

	if (stream->curoff < 0)
		stream->curoff = 0;

	if (stream->curoff >= stream->maxsize)
		stream->curoff = stream->maxsize;

	return 0;
}

size_t cswrite(const char * ptr, size_t size, size_t count, CODE * stream) {
	unsigned int t;

	size *= count;
	if (size + stream->curoff >= stream->maxsize) {
		size = stream->maxsize - stream->curoff;
	}

	char *pos = &stream->data[stream->curoff];
	for (t = 0; t < size; t++) {
		*(pos++) = *ptr++;
	}

	stream->curoff += size;
	if (stream->curend < stream->curoff)
		stream->curend = stream->curoff;

	return size;
}

size_t csread(char * ptr, size_t size, size_t count, CODE * stream) {
	size_t t = 0;

	count *= size;
	while (count-- > 0 && stream->curoff < stream->curend) {
		ptr[t++] = stream->data[stream->curoff++];
	}

	return t;
}

void csdescribe(CODE * stream) {
	printf("File   [%s]\n", stream->filename);
	printf("Cursor [%u]\n", stream->curoff);
	printf("Size   [%u]\n", stream->curend);
	if (stream->maxsize != MAX_JAVASCRIPT_CODE_LEN)
		printf("MaxSize[%u]\n", stream->maxsize);
}

int csclose(CODE * stream) {
	printf("[CLOSE FILE]\n");
	csdescribe(stream);
	return (EOF);
}

#ifdef CONFIG_CODE_MEMORY_TESTING
void main() {
	CODE *myfile;

	myfile = csopen("test.js", "rw+");
	printf(" Getting memory %p \n", myfile);

	cswrite("01234567890123456789\0", 21, sizeof(char), myfile);
	printf("[%s] %i \n", myfile->data, myfile->curoff);

	csseek(myfile, 10, SEEK_SET);
	cswrite("ABCDEFGHIK\0", 11, sizeof(char), myfile);
	printf("[%s] %i \n", myfile->data, myfile->curoff);

	csseek(myfile, 5, SEEK_SET);
	cswrite("01234", 5, sizeof(char), myfile);
	printf("[%s] %i \n", myfile->data, myfile->curoff);

	cswrite("01234\0", 6, sizeof(char), myfile);
	printf("[%s] %i \n", myfile->data, myfile->curoff);

	csseek(myfile, -10, SEEK_END);
	cswrite("012345", 5, sizeof(char), myfile);
	printf("[%s] %i \n", myfile->data, myfile->curoff);

	printf(" End \n");
}
#endif
