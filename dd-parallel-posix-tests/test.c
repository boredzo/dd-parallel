//
//  test.c
//  dd-parallel-posix-tests
//
//  Created by Peter Hosey on 2022-08-28.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#include <sys/types.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "formatting_utils.h"

struct test_case {
	char test_name[16];
	char const *const(*test_proc)(void);
};

static char const *const test_bytecount_20bytes(void);
static char const *const test_bytecount_1KiB(void);
static char const *const test_bytecount_1MiB(void);
static char const *const test_bytecount_3HalfMiB(void);
static char const *const test_interval_500ms(void);
static char const *const test_interval_1sec(void);
static char const *const test_interval_1min(void);
static char const *const test_interval_1hr(void);
static char const *const test_interval_1day(void);
static char const *const test_interval_1d1h1m1s(void);

enum { num_all_cases = 4 + 5 + 1 };
static struct test_case const all_cases[num_all_cases] = {
	{ "bytes_20_bytes", test_bytecount_20bytes, },
	{ "bytes_1_KiB", test_bytecount_1KiB, },
	{ "bytes_1_MiB", test_bytecount_1MiB, },
	{ "bytes_1.5_MiB", test_bytecount_3HalfMiB, },

	{ "interval_ms", test_interval_500ms, },
	{ "interval_sec", test_interval_1sec, },
	{ "interval_min", test_interval_1min, },
	{ "interval_hr", test_interval_1hr, },
	{ "interval_d", test_interval_1day, },

	{ "interval_dhms", test_interval_1d1h1m1s, },
};

#define ASCII_BKSP "\x08"

int main(int argc, const char * argv[]) {
	//TODO: If any test names are passed in on the command line, only run those tests.

	for (unsigned int i = 0; i < num_all_cases; ++i) {
		printf("%s…", all_cases[i].test_name);
		char const *const failureString = all_cases[i].test_proc();
		if (failureString != NULL) {
			printf(ASCII_BKSP " failed: %s\n", failureString);
		} else {
			printf(ASCII_BKSP " passed\n");
		}
	}
	return 0;
}

static char const *const test_bytecount_20bytes(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyByteCountPhrase(buffer, 20, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "20 bytes";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_bytecount_1KiB(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyByteCountPhrase(buffer, 1024, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 KiB";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_bytecount_1MiB(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyByteCountPhrase(buffer, 1024 * 1024, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 MiB";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}

static char const *const test_bytecount_3HalfMiB(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyByteCountPhrase(buffer, 1024 * 1024 + 512 * 1024, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1.50 MiB";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_500ms(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 0.5, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "500 ms";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_1sec(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 1.0, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 sec";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_1min(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 60.0, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 min";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_1hr(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 60.0 * 60.0, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 hr";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_1day(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 60.0 * 60.0 * 24.0, bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 d";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
static char const *const test_interval_1d1h1m1s(void) {
	enum { bufferCapacity = 32 };
	char buffer[bufferCapacity] = "FAILURE";
	size_t const len = copyIntervalPhrase(buffer, 0.0
		+ 60.0 * 60.0 * 24.0
		+ 60.0 * 60.0
		+ 60.0
		+ 1.0,
		bufferCapacity);
	static char const expectedStr[bufferCapacity] = "1 d 1 hr 1 min 1 sec";
	bool const rightLen = (len == strlen(expectedStr));
	bool const rightStr = (0 == strcmp(buffer, expectedStr));
	if (! rightLen) return "Incorrect length";
	if (! rightStr) return "Incorrect string generated";
	return NULL;
}
