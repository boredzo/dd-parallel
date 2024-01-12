//
//  main.c
//  dd-parallel-posix/mktest
//
//  Created by Peter Hosey on 2024-01-10.
//  Copyright © 2024 Peter Hosey. All rights reserved.
//

//This tool generates a test file in which each mebibyte is serially numbered. The first 16 bytes are allocated to an ASCII representation of the number; after that, the number is splatted down the rest of the chunk as a 32-bit number.

#include "formatting_utils.h"

//For htonl
#include <arpa/inet.h>

#if SHOW_DEBUG_LOGGING
#	define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#	define LOG(...) if (0) 0
#endif

#define MILLIONS(a,b,c) a##b##c
//See dd-parallel-posix/main.c for why this number was chosen. (If that file has a different value, please file a bug report or submit a patch.)
static const size_t kBufferSize = MILLIONS(1,048,576);

typedef double time_fractional_t;
typedef unsigned int UnsignedInt32;

static time_fractional_t timeWithFraction(void);

static time_fractional_t copyStartedTime, copyFinishedTime;
static unsigned long long _Atomic totalAmountCopied = 0;
static UnsignedInt32 _Atomic serialNumber = 0; //By definition, is equal to totalAmountCopied / kBufferSize.
static int outputFD;
static void *buffer;

static void logProgress(bool const isFinal);
static void handleSIGINFO(int const signal);
static bool pathIsHyphen(char const *const arg);
static unsigned long long parseSize(char const *arg);
static void fillBuffer(void *buf, UnsignedInt32 serialNumber);

int main(int argc, const char * argv[]) {
	//TEMP FIXME Need actual argument handling here
	unsigned long long desiredSize = parseSize(argv[1]);
	outputFD = pathIsHyphen(argv[2]) ? STDOUT_FILENO : open(argv[2], O_WRONLY | O_CREAT, 0644);
	if (outputFD < 0) return EX_CANTCREAT;

	buffer = malloc(kBufferSize);
	if (buffer == NULL) return EX_OSERR;

	struct sigaction onSIGINFO = {
		.sa_handler = handleSIGINFO,
		.sa_mask = 0,
		.sa_flags = SA_RESTART,
	};
	sigaction(SIGINFO, &onSIGINFO, /*outPrevious*/ NULL);

	int status = EXIT_SUCCESS;

	copyStartedTime = timeWithFraction();

	while (totalAmountCopied < desiredSize) {
		fillBuffer(buffer, serialNumber);
		ssize_t thisAmountCopied = write(outputFD, buffer, kBufferSize);
		if (thisAmountCopied < 0) {
			fprintf(stderr, "Write of block #%u failed: %s\n", serialNumber, strerror(errno));
			status = EX_IOERR;
			break;
		}
		++serialNumber;
		totalAmountCopied += thisAmountCopied;
	}

	free(buffer);

	ftruncate(outputFD, totalAmountCopied);
	copyFinishedTime = timeWithFraction();
	logProgress(true);

	return status;
}

static bool pathIsHyphen(char const *const arg) {
	return arg[0] == '-' && arg[1] == '\0';
}
static unsigned long long parseSize(char const *arg) {
	unsigned long long wholePart = strtoull(arg, (char **)&arg, 10);
	unsigned long long decimalPart = 0ULL;
	if (*arg == '.') {
		++arg;
		decimalPart = strtoull(arg, (char **)&arg, 10);
	}
	switch (*arg) {
		case 'p':
		case 'P':
			wholePart *= 1024;
		case 'e':
		case 'E':
			wholePart *= 1024;
		case 't':
		case 'T':
			wholePart *= 1024;
		case 'g':
		case 'G':
			wholePart *= 1024;
		case 'm':
		case 'M':
			wholePart *= 1024;
		case 'k':
		case 'K':
			wholePart *= 1024;
	}
	//TODO: Implement decimalPart
	return wholePart;
}

static void fillBuffer(void *buf, UnsignedInt32 serialNumber) {
	enum { maxIdx = kBufferSize / sizeof(UnsignedInt32) };
	register UnsignedInt32 *buf32 = buf;
	register UnsignedInt32 value = htonl(serialNumber);
	for (register unsigned int i = 0U; i < maxIdx; ++i) {
		buf32[i] = value;
	}
	sprintf(buf, "%'-12u%c%c%c\n", serialNumber, 0, 0, 0);
}

#pragma mark -

static time_fractional_t timeWithFraction(void) {
	struct timespec now;
	clock_gettime(CLOCK_THEGOODONE, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

static void logProgress(bool const isFinal) {
	time_fractional_t const now = isFinal ? copyFinishedTime : timeWithFraction();
	time_fractional_t const numSecs = now - copyStartedTime;
	unsigned long long const bytesCopiedSoFar = totalAmountCopied;
	double const bytesPerSec = bytesCopiedSoFar / numSecs;

	enum { maxMessageLen = 255, maxMessageCapacity };
	char message[maxMessageCapacity] = { 0 };
	size_t messageLen = strlcpy(message, isFinal ? "Copied " : "Have copied ", maxMessageCapacity);
	char *_Nonnull dst = message + messageLen;
	messageLen += copyByteCountPhrase(dst, bytesCopiedSoFar, maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;
	dst = message + messageLen;
	messageLen += strlcat(dst, " in ", maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;
	dst = message + messageLen;
//		messageLen += snprintf(dst, maxMessageCapacity - messageLen, "%f seconds = ", numSecs);
//		dst = message + messageLen;
	messageLen += copyIntervalPhrase(dst, numSecs, maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;
	dst = message + messageLen;
	messageLen += strlcat(dst, " (overall avg ", maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;
	dst = message + messageLen;
	messageLen += copyByteCountPhrase(dst, bytesPerSec, maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;
	dst = message + messageLen;
	messageLen += strlcat(dst, "/sec)", maxMessageCapacity - messageLen);
	if (messageLen >= maxMessageLen) goto printMessage;

printMessage:
	printf("%s\n", message);
}
static void handleSIGINFO(int const signal) {
	logProgress(false);
}
