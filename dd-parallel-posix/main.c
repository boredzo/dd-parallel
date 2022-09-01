//
//  main.c
//  dd-parallel-posix
//
//  Created by Peter Hosey on 2022-08-28.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

//This version of dd-parallel uses no Apple-specific APIs: No dispatch, no Foundation, etc.
//It uses pthreads, atomics, and locks to do the concurrent reading and writing.

//*** For system header includes, see prefix-*.h. The Xcode project uses prefix-Darwin.h, and the Makefile automatically selects one based on the output of uname.

#include "formatting_utils.h"

#if SHOW_DEBUG_LOGGING
#	define LOG(...) fprintf(stderr, __VA_ARGS__)
#else
#	define LOG(...) if (0) 0
#endif

#if ! __has_extension(nullability)
#	define _Nonnull /*non-null*/
#	define _Nullable /*nullable*/
#endif

typedef double time_fractional_t;
///Returns a number of seconds since… something or other. Whatever CLOCK_UPTIME_RAW counts.
static time_fractional_t timeWithFraction(void);

#define MILLIONS(a,b,c) a##b##c
//https://lists.apple.com/archives/filesystem-dev/2012/Feb/msg00015.html suggests that the optimal chunk size is somewhere between 128 KiB (USB packet size) and 1 MiB.
//I've tested 128 KiB, 1 MiB, and 10 MiB (which is what I used to use in an earlier version of this code and had previously been using with dd) and couldn't detect a statistically significant difference. I'd need to graph out the copying speed over time to properly correlate the difference, and it might still be within the margin of error.
//Absent any conclusive reason to do otherwise, I'm going with the upper bound of the range that (presumably) Apple file-systems engineer gave.
static const size_t kBufferSize = MILLIONS(1,048,576);

static time_fractional_t copyStartedTime, copyFinishedTime;
static unsigned long long totalAmountCopied = 0;
static int inputFD, outputFD;
static void *buffer0, *buffer1;
static bool _Atomic buffer0Dirty = true, buffer1Dirty = true; //true when there is data here that has not been written. Set to false by the writer and set to true again by the writer. When both are false, the writer thread exits.
static size_t _Atomic buffer0Len = 1, buffer1Len = 1; //How much data was most recently read into each buffer.
static pthread_mutex_t initializationLock = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
static pthread_rwlock_t buffer0Lock = PTHREAD_RWLOCK_INITIALIZER, buffer1Lock = PTHREAD_RWLOCK_INITIALIZER;
static unsigned long _Atomic readGeneration0 = 0, readGeneration1 = 0;
static unsigned long _Atomic writeGeneration0 = 0, writeGeneration1 = 0;
static int _Atomic mostRecentlyReadBuffer = -1;
static enum {
	state_beforeFirstRead,
	state_readBegun,
	state_readFinished,
	state_readFailed,
	state_endOfFile,
} _Atomic readerState;
static enum {
	state_beforeFirstWrite,
	state_writeBegun,
	state_writeFinished,
	state_writeFailed,
} _Atomic writerState;

static void logProgress(bool const isFinal);
static void handleSIGINFO(int const signal);
static void *read_thread_main(void *restrict arg);
static void *write_thread_main(void *restrict arg);

int main(int argc, const char * argv[]) {
	//TEMP FIXME Need actual argument handling here
	inputFD = open(argv[1], O_RDONLY);
	if (inputFD < 0) return EX_NOINPUT;
	outputFD = open(argv[2], O_WRONLY | O_CREAT, 0644);
	if (outputFD < 0) return EX_CANTCREAT;

	readerState = state_beforeFirstRead;
	writerState = state_beforeFirstWrite;
	buffer0 = malloc(kBufferSize);
	buffer1 = malloc(kBufferSize);
	if (buffer0 == NULL || buffer1 == NULL) return EX_OSERR;

	pthread_mutex_lock(&initializationLock);

	pthread_t read_thread, write_thread;
	pthread_create(&read_thread, /*attr*/ NULL, read_thread_main, /*user data*/ NULL);

	pthread_mutex_unlock(&initializationLock);
	sleep(0);

	pthread_create(&write_thread, /*attr*/ NULL, write_thread_main, /*user data*/ NULL);

	signal(SIGINFO, handleSIGINFO);

	void *_Nullable retval;
	pthread_join(read_thread, &retval);
	pthread_join(write_thread, &retval);
	free(buffer1);
	free(buffer0);

	ftruncate(outputFD, totalAmountCopied);
	copyFinishedTime = timeWithFraction();
	logProgress(true);

	return EXIT_SUCCESS;
}

static void *read_thread_main(void *restrict arg) {
	pthread_setname_np("Reader thread");
	if (readerState != state_beforeFirstRead) return "Reader starting in bad state";

	if (pthread_mutex_lock(&initializationLock) == EDEADLK) return "Reader deadlocked on init lock";

	copyStartedTime = timeWithFraction();
	pthread_rwlock_rdlock(&buffer0Lock);
	LOG("Reading into buffer %d\n", 0);
	readerState = state_readBegun;
	ssize_t readResult = read(inputFD, buffer0, kBufferSize);
	if (readResult >= 0) {
		buffer0Len = readResult;
		mostRecentlyReadBuffer = 0;
		++readGeneration0;
		readerState = state_readFinished;
	} else {
		readerState = state_readFailed;
	}
	LOG("Finished reading into buffer %d\n", 0);
	pthread_rwlock_unlock(&buffer0Lock);
	pthread_mutex_unlock(&initializationLock);

	void *_Nonnull buffers[2] = { buffer0, buffer1 };
	size_t _Atomic *lengths[2] = { &buffer0Len, &buffer1Len };
	bool _Atomic *dirtyBits[2] = { &buffer0Dirty, &buffer1Dirty };
	unsigned long _Atomic *generations[2] = { &readGeneration0, &readGeneration1 };
	pthread_rwlock_t *locks[2] = { &buffer0Lock, &buffer1Lock };
	int nextBufferIdx = !mostRecentlyReadBuffer;

	while (readResult > 0) {
		LOG("Waiting to read into buffer %d…\n", nextBufferIdx);
		pthread_rwlock_rdlock(locks[nextBufferIdx]);
		LOG("Reading into buffer %d\n", nextBufferIdx);

		readerState = state_readBegun;
		*dirtyBits[nextBufferIdx] = true;
		readResult = read(inputFD, buffers[nextBufferIdx], kBufferSize);
		if (readResult >= 0) {
			*lengths[nextBufferIdx] = readResult;
			++*(generations[nextBufferIdx]);
			mostRecentlyReadBuffer = nextBufferIdx;
			readerState = state_readFinished;
		} else {
			LOG("Read failure\n");
			readerState = state_readFailed;
		}
		LOG("Finished reading buffer %d\n", nextBufferIdx);
		if (readResult == 0) {
			readerState = state_endOfFile;
			LOG("Read loop reached end of input file\n");
		}
		pthread_rwlock_unlock(locks[nextBufferIdx]);
		if (readResult == 0) {
			break;
		}

		nextBufferIdx = !mostRecentlyReadBuffer;
	}
	LOG("Read loop exiting\n");
	return readerState == state_readFailed ? "Reader failed" : NULL;
}

static void *write_thread_main(void *restrict arg) {
	pthread_setname_np("Writer thread");
	if (writerState != state_beforeFirstRead) return "Writer starting in bad state";

	if (pthread_mutex_lock(&initializationLock) == EDEADLK) return "Writer deadlocked on init lock";
	pthread_mutex_unlock(&initializationLock);

	int curBufferIdx = mostRecentlyReadBuffer;
	void *_Nonnull buffers[2] = { buffer0, buffer1 };
	size_t _Atomic *lengths[2] = { &buffer0Len, &buffer1Len };
	bool _Atomic *dirtyBits[2] = { &buffer0Dirty, &buffer1Dirty };
	unsigned long _Atomic *readGenerations[2] = { &readGeneration0, &readGeneration1 };
	unsigned long _Atomic *writeGenerations[2] = { &writeGeneration0, &writeGeneration1 };
	pthread_rwlock_t *locks[2] = { &buffer0Lock, &buffer1Lock };

	while (readerState != state_endOfFile) {
		LOG("Waiting to write buffer %d (reader state is %d)…\n", curBufferIdx, readerState);
		pthread_rwlock_wrlock(locks[curBufferIdx]);
		writerState = state_writeBegun;
		LOG("Writing buffer %d\n", curBufferIdx);
		ssize_t offset = 0;
		size_t const amtToWrite = *lengths[curBufferIdx];
		while (offset < amtToWrite) {
			ssize_t const amtWritten = write(outputFD, buffers[curBufferIdx] + offset, amtToWrite - offset);
			if (amtWritten < 0) {
				writerState = state_writeFailed;
				LOG("Write failure");
				return "Failed write";
			}
			offset += amtWritten;
			totalAmountCopied += amtWritten;
		}
		*dirtyBits[curBufferIdx] = false;
		LOG("Finished writing buffer %d\n", curBufferIdx);
		int const nextBufferIdx = !curBufferIdx;
		++*writeGenerations[curBufferIdx];
		writerState = state_writeFinished;
		pthread_rwlock_unlock(locks[curBufferIdx]);
		curBufferIdx = nextBufferIdx;
	}
	LOG("Write loop exiting because readerState is %d", readerState);
	return NULL;
}

#pragma mark -

static time_fractional_t timeWithFraction(void) {
	struct timespec now;
	clock_gettime(CLOCK_UPTIME_RAW, &now);
	return now.tv_sec + now.tv_nsec / 1e9;
}

static void logProgress(bool const isFinal) {
	if (readerState == state_beforeFirstRead) {
		printf("Copy has not started yet.");
	} else {
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
}
static void handleSIGINFO(int const signal) {
	logProgress(false);
}
