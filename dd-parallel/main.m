//
//  main.m
//  dd-parallel
//
//  Created by Peter Hosey on 2014-11-02.
//  Copyright (c) 2014 Peter Hosey. All rights reserved.
//

#import <Foundation/Foundation.h>
#include <sysexits.h>
#import <os/log.h>
#if INCLUDE_SIGNPOSTS
//Signposts are a tool for marking up the tool's activity in Instruments. They're very helpful for that purpose, but only available in 10.14 and later.
#import <os/signpost.h>
#endif

#import "PRHMD5Context.h"
#import "PRHProgressReporter.h"

#define MILLIONS(a,b,c) a##b##c
//https://lists.apple.com/archives/filesystem-dev/2012/Feb/msg00015.html suggests that the optimal chunk size is somewhere between 128 KiB (USB packet size) and 1 MiB.
//I've tested 128 KiB, 1 MiB, and 10 MiB (which is what I used to use in an earlier version of this code and had previously been using with dd) and couldn't detect a statistically significant difference. I'd need to graph out the copying speed over time to properly correlate the difference, and it might still be within the margin of error.
//Absent any conclusive reason to do otherwise, I'm going with the upper bound of the range that (presumably) Apple file-systems engineer gave.
static const size_t kBufferSize = MILLIONS(1,048,576);

#define ONE_MINUTE_NSEC (60 * NSEC_PER_SEC)

static const bool verbose = false;

static _Atomic unsigned long long bytesCopiedSoFar = 0;
static dispatch_queue_t writeQueue = NULL;
static dispatch_queue_t logQueue = NULL;
static PRHProgressReporter *sProgressReporter;

static void handleSIGINFO(int signalThatWasCaught) {
	dispatch_async(writeQueue, ^{
		NSTimeInterval now = [NSDate timeIntervalSinceReferenceDate];
		unsigned long long const capturedBCSF = bytesCopiedSoFar;
		dispatch_async(logQueue, ^{
			[sProgressReporter reportProgressAsOfInstant:now
				cumulativeBytesCopied:capturedBCSF
				isFinal:false];
		});
	});
}

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

	signal(SIGINFO, handleSIGINFO);

	@autoreleasepool {
		dispatch_queue_t readQueue = dispatch_queue_create("read queue", DISPATCH_QUEUE_SERIAL);
		writeQueue = dispatch_queue_create("write queue", DISPATCH_QUEUE_SERIAL);
		logQueue = dispatch_queue_create("log queue", DISPATCH_QUEUE_SERIAL);

		void *buffers[2] = {
			malloc(kBufferSize),
			malloc(kBufferSize)
		};
		__block int currentBufferIdx = 0;

		int inFD = open(inputPath, O_RDONLY);
		if (inFD < 0) {
			NSLog(@"Couldn't open %s: %s\n", inputPath, strerror(errno));
			return EX_NOINPUT;
		}
		//We intentionally omit O_TRUNC here so we can reuse existing blocks instead of having to force the file-system to reallocate. We separately truncate to the written length at the end.
		int outFD = open(outputPath, O_WRONLY | O_EXLOCK);
		if (outFD < 0) {
			NSLog(@"Couldn't open %s: %s\n", outputPath, strerror(errno));
			return EX_CANTCREAT;
		}

		fcntl(inFD, F_RDAHEAD, 1);

		//Without this, between rdisks: ~130 MB/sec.
		//With this, between rdisks: ~140 MB/sec.
		fcntl(inFD, F_NOCACHE, 1);
		fcntl(outFD, F_NOCACHE, 1);

		__block _Atomic unsigned long long totalAmountWritten = 0;
		sProgressReporter = [PRHProgressReporter new];

		PRHMD5Context *_Nonnull const readMD5Context = [PRHMD5Context new];
		unsigned char readMD5Digest[PRHMD5DigestNumberOfBytes] = { 0 };
		unsigned char *const readMD5DigestPtr = readMD5Digest;
		PRHMD5Context *_Nonnull const writeMD5Context = [PRHMD5Context new];
		unsigned char writeMD5Digest[PRHMD5DigestNumberOfBytes] = { 0 };
		unsigned char *const writeMD5DigestPtr = writeMD5Digest;

		__block int readNumber = 0;
		__block int writeNumber = 0;
#if INCLUDE_SIGNPOSTS
		os_log_t signpostLog = os_log_create("org.boredzo.dd-parallel", "signposts");
#endif

		ssize_t (^readBlock)(void *currentBuffer, unsigned char *const readMD5DigestPtr) = ^(void *currentBuffer, unsigned char *const readMD5DigestPtr){
#if INCLUDE_SIGNPOSTS
			os_signpost_id_t signpostID = os_signpost_id_generate(signpostLog);
			os_signpost_interval_begin(signpostLog, signpostID, "read", "Read begins");
#endif
			ssize_t const amtRead = read(inFD, currentBuffer, kBufferSize);

			if (checkMD5) {
				[readMD5Context updateWithBytes:currentBuffer length:amtRead];
			}

			++readNumber;

			if (verbose) {
				NSString *_Nonnull const readMessage = [NSString stringWithFormat:@"Read number #%d; first bytes are 0x%02hhx%02hhx%02hhx%02hhx",
					readNumber,
					((const char *)currentBuffer)[0], ((const char *)currentBuffer)[1], ((const char *)currentBuffer)[2], ((const char *)currentBuffer)[3]
				];
				dispatch_async(logQueue, ^{
					fprintf(stderr, "%s\n", readMessage.UTF8String);
				});
			}

			if (checkMD5) {
				[readMD5Context peekAtDigestBytes:readMD5DigestPtr];
				NSString *_Nonnull const readMD5Message = [NSString stringWithFormat:@"Read number #%d; first bytes of read MD5 are 0x%02hhx%02hhx%02hhx%02hhx",
					readNumber,
					readMD5DigestPtr[0], readMD5DigestPtr[1], readMD5DigestPtr[2], readMD5DigestPtr[3]
				];
				dispatch_async(logQueue, ^{
					fprintf(stderr, "%s\n", readMD5Message.UTF8String);
				});
			}

#if INCLUDE_SIGNPOSTS
			os_signpost_interval_end(signpostLog, signpostID, "read", "Read ends");
#endif
			return amtRead;
		};

		void (^writeBlock)(void *currentBuffer, size_t const numBytesToWrite, unsigned char *const writeMD5DigestPtr) = ^(void *currentBuffer, size_t numBytesToWrite, unsigned char *const writeMD5DigestPtr){
#if INCLUDE_SIGNPOSTS
			os_signpost_id_t signpostID = os_signpost_id_generate(signpostLog);
			os_signpost_interval_begin(signpostLog, signpostID, "write", "Write begins");
#endif

			++writeNumber;

			if (verbose) {
				NSString *_Nonnull const message = [NSString stringWithFormat:@"Write number #%d; first bytes are 0x%02hhx%02hhx%02hhx%02hhx",
					writeNumber,
					((const char *)currentBuffer)[0], ((const char *)currentBuffer)[1], ((const char *)currentBuffer)[2], ((const char *)currentBuffer)[3]
				];
				dispatch_async(logQueue, ^{
					fprintf(stderr, "%s\n", message.UTF8String);
				});
			}
			ssize_t amtWritten = 0;
			ssize_t thisWrite;
			const void *bytesYetToBeWritten = currentBuffer;
			size_t numBytesYetToBeWritten = numBytesToWrite;
			while (0 < (thisWrite = write(outFD, bytesYetToBeWritten, numBytesYetToBeWritten))) {
				if (checkMD5) {
					[writeMD5Context updateWithBytes:bytesYetToBeWritten length:thisWrite];

					[writeMD5Context peekAtDigestBytes:writeMD5DigestPtr];
					if (verbose) {
						NSString *_Nonnull const message = [NSString stringWithFormat:@"Write number #%d; first bytes of write MD5 are 0x%02hhx%02hhx%02hhx%02hhx",
							writeNumber,
							writeMD5DigestPtr[0], writeMD5DigestPtr[1], writeMD5DigestPtr[2], writeMD5DigestPtr[3]
						];
						dispatch_async(logQueue, ^{
							fprintf(stderr, "%s\n", message.UTF8String);
						});
					}
				}

				amtWritten += thisWrite;
				bytesYetToBeWritten += thisWrite;
				numBytesYetToBeWritten -= thisWrite;
				if (amtWritten == numBytesToWrite) {
					break;
				}
			}
			totalAmountWritten += amtWritten;
			bytesCopiedSoFar = totalAmountWritten;
			NSTimeInterval const instantAfterWrite = [NSDate timeIntervalSinceReferenceDate];
			dispatch_async(logQueue, ^{
				[sProgressReporter recordProgressAsOfInstant:instantAfterWrite cumulativeBytesCopied:bytesCopiedSoFar];
			});

			if (thisWrite < 0) {
				int writeError = errno;
				dispatch_sync(logQueue, ^{
					fprintf(stderr, "Error during write: %s\n", strerror(writeError));
					exit(EX_IOERR);
				});
			}
#if INCLUDE_SIGNPOSTS
			os_signpost_interval_end(signpostLog, signpostID, "write", "Write ends");
#endif
		};

		NSTimeInterval start = [NSDate timeIntervalSinceReferenceDate];
		sProgressReporter.whenCopyingStarted = start;

		ssize_t amtRead = readBlock(buffers[currentBufferIdx], readMD5Digest);

		while (amtRead > 0) {
			unsigned char *const writeBuffer = buffers[currentBufferIdx];
			dispatch_async(writeQueue, ^{ writeBlock(writeBuffer, (size_t)amtRead, writeMD5DigestPtr); });

			currentBufferIdx = ! currentBufferIdx;
			unsigned char *const readBuffer = buffers[currentBufferIdx];

			NSData *const frozenReadDigestBytes = checkMD5 ? [NSData dataWithBytes:readMD5Digest length:PRHMD5DigestNumberOfBytes] : nil;

			__block ssize_t nextAmtRead = 0;
			dispatch_async(readQueue, ^{ nextAmtRead = readBlock(readBuffer, readMD5DigestPtr); });

#if INCLUDE_SIGNPOSTS
			os_signpost_id_t signpostID = os_signpost_id_generate(signpostLog);
			os_signpost_interval_begin(signpostLog, signpostID, "between", "Between begins");
#endif

			//Wait on the read queue first because reads are usually faster than writes, so we'll be doing this wait while we're still writing.
			dispatch_barrier_sync(readQueue, ^{});
			amtRead = nextAmtRead;

			//We do need to wait on the write queue too, so we don't get ahead of ourselves.
			dispatch_barrier_sync(writeQueue, ^{});

			if (checkMD5) {
				const unsigned char *readMD5DigestPtr = frozenReadDigestBytes.bytes;
				NSString *_Nonnull const message = [NSString stringWithFormat:
					@"After write number #%d; first bytes of read MD5 are 0x%02hhx%02hhx%02hhx%02hhx\n"
					@"After write number #%d; first bytes of write MD5 are 0x%02hhx%02hhx%02hhx%02hhx\n",
					writeNumber,
					readMD5DigestPtr[0], readMD5DigestPtr[1], readMD5DigestPtr[2], readMD5DigestPtr[3],
					writeNumber,
					writeMD5DigestPtr[0], writeMD5DigestPtr[1], writeMD5DigestPtr[2], writeMD5DigestPtr[3]
				];
				dispatch_async(logQueue, ^{
					fprintf(stderr, "%s", message.UTF8String);
				});

				if (0 != memcmp(frozenReadDigestBytes.bytes, writeMD5DigestPtr, PRHMD5DigestNumberOfBytes)) {
					dispatch_sync(logQueue, ^{
						dispatch_barrier_sync(readQueue, ^{});
						dispatch_barrier_sync(writeQueue, ^{});

						fprintf(stderr, "Oh crap! MD5s no longer match between read and write!\nRead:  %s\nWrite: %s\nBailing now!\n", [readMD5Context stringDescribingDigestBytes:(unsigned char *)frozenReadDigestBytes.bytes].UTF8String, [writeMD5Context stringDescribingDigestBytes:writeMD5DigestPtr].UTF8String);
						exit(EXIT_FAILURE);
					});
				}
			}

#if INCLUDE_SIGNPOSTS
			os_signpost_interval_end(signpostLog, signpostID, "between", "Between ends");
#endif
		}

		ftruncate(outFD, totalAmountWritten); //May fail if this is a device; we don't care.
		close(inFD);
		close(outFD);
		free(buffers[0]);
		free(buffers[1]);

		NSTimeInterval end = [NSDate timeIntervalSinceReferenceDate];
		[sProgressReporter reportProgressAsOfInstant:end
			cumulativeBytesCopied:totalAmountWritten
			isFinal:true];

		if (checkMD5) {
			[readMD5Context finalizeAndGetDigestBytes:readMD5Digest];
			[writeMD5Context finalizeAndGetDigestBytes:writeMD5Digest];
			dispatch_sync(logQueue, ^{
				printf("%s\t%s\n", [readMD5Context stringDescribingDigestBytes:readMD5DigestPtr].UTF8String, inputPath);
				printf("%s\t%s\n", [writeMD5Context stringDescribingDigestBytes:writeMD5DigestPtr].UTF8String, outputPath);
			});
			if (0 != memcmp(readMD5DigestPtr, writeMD5DigestPtr, PRHMD5DigestNumberOfBytes)) {
				dispatch_sync(logQueue, ^{
					fprintf(stderr, "Corruption detected! The MD5 of what was written does not match the MD5 of what was read! YOU SHOULD NOT TRUST THE COPY!!!!\n");
					exit(EXIT_FAILURE);
				});
			}
		}
	}
    return EXIT_SUCCESS;
}

