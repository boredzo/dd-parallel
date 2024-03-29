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

typedef double time_fractional_t;
///Returns a number of seconds since… something or other. Whatever CLOCK_THEGOODONE counts.
static time_fractional_t timeWithFraction(void);

#define MILLIONS(a,b,c) a##b##c
//https://lists.apple.com/archives/filesystem-dev/2012/Feb/msg00015.html suggests that the optimal chunk size is somewhere between 128 KiB (USB packet size) and 1 MiB.
//I've tested 128 KiB, 1 MiB, and 10 MiB (which is what I used to use in an earlier version of this code and had previously been using with dd) and couldn't detect a statistically significant difference. I'd need to graph out the copying speed over time to properly correlate the difference, and it might still be within the margin of error.
//Absent any conclusive reason to do otherwise, I'm going with the upper bound of the range that (presumably) Apple file-systems engineer gave.
static const size_t kBufferSize = MILLIONS(1,048,576);

static time_fractional_t copyStartedTime, copyFinishedTime;
static unsigned long long _Atomic totalAmountCopied = 0;
static int inputFD, outputFD;
static void *buffer0, *buffer1;
static bool _Atomic buffer0Dirty = true, buffer1Dirty = true; //true when there is data here that has not been written. Set to false by the writer and set to true again by the writer. When both are false, the writer thread exits.
static size_t _Atomic buffer0Len = 1, buffer1Len = 1; //How much data was most recently read into each buffer.
static pthread_mutex_t initializationLock = PTHREAD_ERRORCHECK_MUTEX_INITIALIZER;
static bool _Atomic readerHasInitialized = false;
static pthread_rwlock_t buffer0Lock = PTHREAD_RWLOCK_INITIALIZER, buffer1Lock = PTHREAD_RWLOCK_INITIALIZER;
//The generation counts—which, as you can see here, are per-buffer—are used to prevent each loop from getting too far ahead of the other. Reading will only proceed when the last write generation is equal to the last read generation; the new read will be the next read generation. Writing will only proceed when the last write generation is one behind the last read generation; the new write, being the next write generation, will catch up to the last read generation.
static unsigned long _Atomic readGeneration0 = 0, readGeneration1 = 0;
static unsigned long _Atomic writeGeneration0 = 0, writeGeneration1 = 0;
static int _Atomic mostRecentlyReadBuffer = -1;
static enum {
	state_beforeFirstRead = '0B4R',
	state_readBegun = '1GO!',
	state_readFinished = '2END',
	state_readFailed = '3RIP',
	state_endOfFile = '4EOF',
} _Atomic readerState = state_beforeFirstRead;
static enum {
	state_beforeFirstWrite = '0B4W',
	state_writeBegun = '1GO!',
	state_writeFinished = '2END',
	state_writeFailed = '3RIP',
} _Atomic writerState = state_beforeFirstWrite;

static char const *reader_nameState(int const readerState) {
	static union {
		struct {
			int readerState;
			int zero;
		} pieces;
		char string[sizeof(int) * 2];
	} nexus;
	nexus.pieces.readerState = htonl(readerState);
	return nexus.string;
}
static char const *writer_nameState(int const writerState) {
	static union {
		struct {
			int writerState;
			int zero;
		} pieces;
		char string[sizeof(int) * 2];
	} nexus;
	nexus.pieces.writerState = htonl(writerState);
	return nexus.string;
}

enum {
	readErrorMaxLength = 255,
	readErrorCapacity,
	writeErrorMaxLength = 255,
	writeErrorCapacity,
};
char readErrorBuffer[readErrorCapacity] = { 0 };
char writeErrorBuffer[writeErrorCapacity] = { 0 };

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

#if EXISTS_F_RDAHEAD
	fcntl(inputFD, F_RDAHEAD, 1);
#endif
#if EXISTS_F_NOCACHE
	fcntl(inputFD, F_NOCACHE, 1);
	fcntl(outputFD, F_NOCACHE, 1);
#endif

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

	struct sigaction onSIGINFO = {
		.sa_handler = handleSIGINFO,
		.sa_mask = 0,
		.sa_flags = SA_RESTART,
	};
	sigaction(SIGINFO, &onSIGINFO, /*outPrevious*/ NULL);

	int status = EXIT_SUCCESS;

	void *_Nullable retval;
	pthread_join(read_thread, &retval);
	if (retval != NULL) {
		char const *_Nonnull const readErrorStr = retval;
		fprintf(stderr, "dd-parallel: error during read: %s\n", readErrorStr);
		status = EX_NOINPUT;
	}
	pthread_join(write_thread, &retval);
	if (retval != NULL) {
		char const *_Nonnull const writeErrorStr = retval;
		fprintf(stderr, "dd-parallel: error during write: %s\n", writeErrorStr);
		if (status == EXIT_SUCCESS) status = EX_IOERR;
	}
	free(buffer1);
	free(buffer0);

	fflush(stderr);
	ftruncate(outputFD, totalAmountCopied);
	copyFinishedTime = timeWithFraction();
	logProgress(true);

	return status;
}

static void *read_thread_main(void *restrict arg) {
	pthread_setname_self("Reader thread");
	if (readerState != state_beforeFirstRead) return "Reader starting in bad state";

	if (pthread_mutex_lock(&initializationLock) == EDEADLK) return "Reader deadlocked on init lock";

	copyStartedTime = timeWithFraction();
	pthread_rwlock_rdlock(&buffer0Lock);
	LOG("R[C=%d] Beginning first read\n", 0);
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
	LOG("R[C=%d, RG=%lu] Finished reading %ld bytes, creating read generation #%lu\n", 0, readGeneration0, readResult, readGeneration0);
	pthread_rwlock_unlock(&buffer0Lock);
	readerHasInitialized = true;
	pthread_mutex_unlock(&initializationLock);

	void *_Nonnull buffers[2] = { buffer0, buffer1 };
	size_t _Atomic *lengths[2] = { &buffer0Len, &buffer1Len };
	bool _Atomic *dirtyBits[2] = { &buffer0Dirty, &buffer1Dirty };
	unsigned long _Atomic *readGenerations[2] = { &readGeneration0, &readGeneration1 };
	unsigned long _Atomic *writeGenerations[2] = { &writeGeneration0, &writeGeneration1 };
	pthread_rwlock_t *locks[2] = { &buffer0Lock, &buffer1Lock };
	int nextBufferIdx = !mostRecentlyReadBuffer;

	while (readResult > 0) {
		LOG("R[C=%d] Waiting to read into buffer…\n", nextBufferIdx);
		pthread_rwlock_rdlock(locks[nextBufferIdx]);
		LOG("R[C=%d] Got read lock!\n", nextBufferIdx);
		unsigned long const curReadGen = *readGenerations[nextBufferIdx];
		unsigned long const curWriteGen = *writeGenerations[nextBufferIdx];
		LOG("R[C=%d, RG=%lu, WG=%lu] Reader generation check\n", nextBufferIdx, curReadGen, curWriteGen);
		if (curReadGen == curWriteGen + 1) {
			//The write of our last read hasn't finished yet. Hold off.
			LOG("Write generation has not advanced. Reader coming around again for another pass...\n");
			pthread_rwlock_unlock(locks[nextBufferIdx]);
			sleep(0);
			continue;
		}
		LOG("R[C=%d] Reading into buffer\n", nextBufferIdx);

		readerState = state_readBegun;
		*dirtyBits[nextBufferIdx] = true;
		readResult = read(inputFD, buffers[nextBufferIdx], kBufferSize);
		if (readResult >= 0) {
			*lengths[nextBufferIdx] = readResult;
			++*(readGenerations[nextBufferIdx]);
			mostRecentlyReadBuffer = nextBufferIdx;
			readerState = state_readFinished;
		} else {
			LOG("R[C=%d] Read failure\n", nextBufferIdx);
			strerror_r(errno, readErrorBuffer, readErrorMaxLength);
			readerState = state_readFailed;
		}
		LOG("R[C=%d, RG=%lu] Finished reading %ld bytes. Read generation increases: %lu → %lu\n", nextBufferIdx, *readGenerations[nextBufferIdx], readResult, curReadGen, *readGenerations[nextBufferIdx]);
		if (readResult == 0) {
			readerState = state_endOfFile;
			LOG("R[C=%d, RG=%lu] Read loop reached end of input file\n", nextBufferIdx, *readGenerations[nextBufferIdx]);
		}
		pthread_rwlock_unlock(locks[nextBufferIdx]);
		if (readResult == 0) {
			break;
		}

		nextBufferIdx = !mostRecentlyReadBuffer;
	}
	LOG("R[RG0=%lu, RG1=%lu] Read loop exiting\n", *readGenerations[0], *readGenerations[1]);
	return readerState == state_readFailed ? readErrorBuffer : NULL;
}

static void *write_thread_main(void *restrict arg) {
	pthread_setname_self("Writer thread");
	if (writerState != state_beforeFirstWrite) return "Writer starting in bad state";

	while (! readerHasInitialized ) {
		if (pthread_mutex_lock(&initializationLock) == EDEADLK) return "Writer deadlocked on init lock";
		pthread_mutex_unlock(&initializationLock);
		sleep(0);
	}

	int curBufferIdx = 0;
	void *_Nonnull buffers[2] = { buffer0, buffer1 };
	size_t _Atomic *lengths[2] = { &buffer0Len, &buffer1Len };
	bool _Atomic *dirtyBits[2] = { &buffer0Dirty, &buffer1Dirty };
	unsigned long _Atomic *readGenerations[2] = { &readGeneration0, &readGeneration1 };
	unsigned long _Atomic *writeGenerations[2] = { &writeGeneration0, &writeGeneration1 };
	pthread_rwlock_t *locks[2] = { &buffer0Lock, &buffer1Lock };

	unsigned long capturedRG0 = *readGenerations[0], capturedRG1 = *readGenerations[1];
	unsigned long capturedWG0 = *writeGenerations[0], capturedWG1 = *writeGenerations[1];
	while (readerState != state_endOfFile || capturedWG0 < capturedRG0 || capturedWG1 < capturedRG1) {
		LOG("W[C=%u, RG=%lu, WG=%lu] Waiting for lock to write buffer %d (reader state is %s)…\n",
			curBufferIdx, curBufferIdx == 0 ? capturedRG0 : capturedRG1, curBufferIdx == 0 ? capturedWG0 : capturedWG1,
			curBufferIdx, reader_nameState(capturedReaderState));
		pthread_rwlock_wrlock(locks[curBufferIdx]);
		unsigned long const curReadGen = *readGenerations[curBufferIdx];
		unsigned long const curWriteGen = *writeGenerations[curBufferIdx];
		LOG("W[C=%u, RG=%lu, WG=%lu] Got lock!\n",
			curBufferIdx, curReadGen, curWriteGen);
		if (curReadGen == curWriteGen) {
			//We lapped the read loop. Wait for it to catch up.
			LOG("W[C=%u] Read generation has not advanced. Writer coming around again for another pass...\n", curBufferIdx);
			pthread_rwlock_unlock(locks[curBufferIdx]);
			sleep(0);
			continue;
		}

		writerState = state_writeBegun;
		LOG("W[C=%u] Writing buffer\n", curBufferIdx);
		ssize_t offset = 0;
		size_t const amtToWrite = *lengths[curBufferIdx];
		while (offset < amtToWrite) {
			ssize_t const amtWritten = write(outputFD, buffers[curBufferIdx] + offset, amtToWrite - offset);
			if (amtWritten < 0) {
				writerState = state_writeFailed;
				LOG("W[C=%u] Write failure", curBufferIdx);
				pthread_rwlock_unlock(locks[curBufferIdx]);
				strerror_r(errno, writeErrorBuffer, writeErrorMaxLength);
				return writeErrorBuffer;
			}
			offset += amtWritten;
			totalAmountCopied += amtWritten;
		}
		*dirtyBits[curBufferIdx] = false;
		++*writeGenerations[curBufferIdx];
		LOG("W[C=%u, WG=%lu] Finished writing buffer. Write generation increases: %lu → %lu\n", curBufferIdx, *writeGenerations[curBufferIdx], curWriteGen, *writeGenerations[curBufferIdx]);
		int const nextBufferIdx = !curBufferIdx;
		writerState = state_writeFinished;
		pthread_rwlock_unlock(locks[curBufferIdx]);
		curBufferIdx = nextBufferIdx;

		capturedRG0 = *readGenerations[0];
		capturedRG1 = *readGenerations[1];
		capturedWG0 = *writeGenerations[0];
		capturedWG1 = *writeGenerations[1];
	}
	LOG("W[WG0=%lu, WG1=%lu] Write loop exiting because reader state is %s and buffer 0 generations are R#%lu, W#%lu, buffer 1 generations are R#%lu, W#%lu\n", capturedWG0, capturedWG1, reader_nameState(capturedReaderState), capturedRG0, capturedWG0, capturedRG1, capturedWG1);
	return NULL;
}

#pragma mark -

static time_fractional_t timeWithFraction(void) {
	struct timespec now;
	clock_gettime(CLOCK_THEGOODONE, &now);
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
