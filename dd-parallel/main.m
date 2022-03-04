//
//  main.m
//  dd-parallel
//
//  Created by Peter Hosey on 2014-11-02.
//  Copyright (c) 2014 Peter Hosey. All rights reserved.
//

#import <Foundation/Foundation.h>
#include <sysexits.h>

#import "PRHMD5Context.h"

#define MILLIONS(a,b,c) a##b##c
static const size_t kBufferSize = MILLIONS(10,000,000);

#define ONE_MINUTE_NSEC (60 * NSEC_PER_SEC)

static const bool verbose = false;

int main(int argc, char *argv[]) {
	bool justAskingForHelp = false;
	if (argc < 2) {
	presentHelp:
		fprintf(justAskingForHelp ? stdout : stderr,
			"Usage: %s in-file out-file\n",
			argv[0] ?: "dd-parallel");
		return justAskingForHelp ? EXIT_SUCCESS : EXIT_FAILURE;
	}
	if (strcmp(argv[1], "--help") == 0) {
		justAskingForHelp = true;
		goto presentHelp;
	}

	bool checkMD5 = false;
	char const *inputPath, *outputPath;
	if (strcmp(argv[1], "--md5") == 0) {
		if (argc < 4) {
			//dd-parallel --md5 inputPath outputPath
			goto presentHelp;
		}
		checkMD5 = true;
		inputPath = argv[2];
		outputPath = argv[3];
	} else {
		if (argc < 3) {
			//dd-parallel inputPath outputPath
			goto presentHelp;
		}
		checkMD5 = false;
		inputPath = argv[1];
		outputPath = argv[2];
	}

	@autoreleasepool {
		dispatch_queue_t writeQueue = dispatch_queue_create("write queue", DISPATCH_QUEUE_SERIAL);
		dispatch_queue_t logQueue = dispatch_queue_create("log queue", DISPATCH_QUEUE_SERIAL);

		void *buffers[2] = {
			malloc(kBufferSize),
			malloc(kBufferSize)
		};
		int currentBufferIdx = 0;

		dispatch_semaphore_t semaphore = dispatch_semaphore_create(0);

		int inFD = open(inputPath, O_RDONLY);
		if (inFD < 0) {
			NSLog(@"Couldn't open %s: %s\n", inputPath, strerror(errno));
			return EX_NOINPUT;
		}
		int outFD = open(outputPath, O_WRONLY | O_TRUNC | O_EXLOCK);
		if (outFD < 0) {
			NSLog(@"Couldn't open %s: %s\n", outputPath, strerror(errno));
			return EX_CANTCREAT;
		}

		fcntl(inFD, F_NOCACHE, 1);
		fcntl(inFD, F_RDAHEAD, 1);
		fcntl(outFD, F_NOCACHE, 1);

		__block unsigned long long totalAmountWritten = 0;
		NSTimeInterval start = [NSDate timeIntervalSinceReferenceDate];
		__block NSTimeInterval lastRateLogDate = 0.0;

		PRHMD5Context *_Nonnull const readMD5Context = [PRHMD5Context new], *_Nonnull const writeMD5Context = [PRHMD5Context new];
		unsigned char readMD5Digest[PRHMD5DigestNumberOfBytes];
		const unsigned char *readMD5DigestPtr = readMD5Digest;

		int readNumber = 0;
		int writeNumber = 0;
		ssize_t amtRead;
		while (0 < (amtRead = read(inFD, buffers[currentBufferIdx], kBufferSize))) {
			const void *currentBuffer = buffers[currentBufferIdx];
			currentBufferIdx = ! currentBufferIdx;

			if (checkMD5) {
				[readMD5Context updateWithBytes:currentBuffer length:amtRead];
				[readMD5Context peekAtDigestBytes:readMD5Digest];
			}

			if (verbose) {
				++readNumber;
				dispatch_sync(logQueue, ^{
					fprintf(stderr, "Read number #%d; first bytes are 0x%02hhx%02hhx%02hhx%02hhx\n",
						readNumber,
						((const char *)currentBuffer)[0], ((const char *)currentBuffer)[1], ((const char *)currentBuffer)[2], ((const char *)currentBuffer)[3]
					);
					if (checkMD5) {
						fprintf(stderr, "Read number #%d; first bytes of read MD5 are 0x%02hhx%02hhx%02hhx%02hhx\n",
								readNumber,
								readMD5DigestPtr[0], readMD5DigestPtr[1], readMD5DigestPtr[2], readMD5DigestPtr[3]
								);
					}
				});
				++writeNumber;
			}

			/*
			//If there's a write in progress, wait until it finishes before we schedule this write and then come back around for another read.
			//If there's no write in progress, this will return immediately.
			dispatch_barrier_sync(writeQueue, ^{});
*/
			NSData *frozenReadDigestBytes = [NSData dataWithBytes:readMD5Digest length:PRHMD5DigestNumberOfBytes];

			dispatch_async(writeQueue, ^{
				if (verbose) {
					dispatch_sync(logQueue, ^{
						fprintf(stderr, "Write number #%d; first bytes are 0x%02hhx%02hhx%02hhx%02hhx\n",
							writeNumber,
							((const char *)currentBuffer)[0], ((const char *)currentBuffer)[1], ((const char *)currentBuffer)[2], ((const char *)currentBuffer)[3]
						);
					});
				}
				ssize_t amtWritten = 0;
				ssize_t thisWrite;
				const void *bytesYetToBeWritten = currentBuffer;
				size_t numBytesYetToBeWritten = (size_t)amtRead;
				while (0 < (thisWrite = write(outFD, bytesYetToBeWritten, numBytesYetToBeWritten))) {
					if (checkMD5) {
						[writeMD5Context updateWithBytes:bytesYetToBeWritten length:thisWrite];

						unsigned char writeMD5Digest[PRHMD5DigestNumberOfBytes];
						[writeMD5Context peekAtDigestBytes:writeMD5Digest];
						const unsigned char *writeMD5DigestPtr = writeMD5Digest;
						if (verbose) {
							dispatch_sync(logQueue, ^{
								fprintf(stderr, "Write number #%d; first bytes of write MD5 are 0x%02hhx%02hhx%02hhx%02hhx\n",
										writeNumber,
										writeMD5DigestPtr[0], writeMD5DigestPtr[1], writeMD5DigestPtr[2], writeMD5DigestPtr[3]
										);
								const unsigned char *readMD5DigestPtr = frozenReadDigestBytes.bytes;
								fprintf(stderr, "Write number #%d; first bytes of read MD5 are 0x%02hhx%02hhx%02hhx%02hhx\n",
										writeNumber,
										readMD5DigestPtr[0], readMD5DigestPtr[1], readMD5DigestPtr[2], readMD5DigestPtr[3]
										);
							});
						}
						if (0 != memcmp(frozenReadDigestBytes.bytes, writeMD5DigestPtr, PRHMD5DigestNumberOfBytes)) {
							dispatch_sync(logQueue, ^{
								fprintf(stderr, "Oh crap! MD5s no longer match between read and write! Bailing now!\n");
								exit(EXIT_FAILURE);
							});
						}
					}

					amtWritten += thisWrite;
					bytesYetToBeWritten += thisWrite;
					numBytesYetToBeWritten -= thisWrite;
					if (amtWritten == amtRead) {
						break;
					}
				}
				totalAmountWritten += amtWritten;

				if (thisWrite < 0) {
					int writeError = errno;
					dispatch_sync(logQueue, ^{
						fprintf(stderr, "Error during write: %s\n", strerror(writeError));
						exit(EX_IOERR);
					});
				}
				dispatch_semaphore_signal(semaphore);

				NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
				if (now - lastRateLogDate > 2.0) {
					dispatch_async(logQueue, ^{
						fprintf(stderr, "Bytes copied so far: %'llu.\nTime so far: %f seconds.\nCurrent rate: %f bytes per second.\n",
							totalAmountWritten,
							now - start,
							totalAmountWritten / (now - start)
						);
						lastRateLogDate = now;
					});
				}
			});
			if (0 != dispatch_semaphore_wait(semaphore, dispatch_time(DISPATCH_TIME_NOW, ONE_MINUTE_NSEC))) {
				fprintf(stderr, "Timed out waiting for write.\n");
				exit(EX_IOERR);
			}
		}

		[readMD5Context finalizeAndGetDigestBytes:readMD5Digest];
		unsigned char writeMD5Digest[PRHMD5DigestNumberOfBytes];
		[writeMD5Context finalizeAndGetDigestBytes:writeMD5Digest];
		const unsigned char *writeMD5DigestPtr = writeMD5Digest;
		if (0 != memcmp(readMD5DigestPtr, writeMD5DigestPtr, PRHMD5DigestNumberOfBytes)) {
			dispatch_sync(logQueue, ^{
				fprintf(stderr, "Corruption detected! The MD5 of what was written does not match the MD5 of what was read! YOU SHOULD NOT TRUST THE COPY!!!!\n");
				exit(EXIT_FAILURE);
			});
		}
	}
    return EXIT_SUCCESS;
}

