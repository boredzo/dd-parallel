//
//  PRHMD5Context.m
//  dd-parallel
//
//  Created by Peter Hosey on 2019-12-12.
//  Copyright © 2019 Peter Hosey. All rights reserved.
//

#import "PRHMD5Context.h"

#include <CommonCrypto/CommonCrypto.h>

@implementation PRHMD5Context
{
	dispatch_queue_t _queue;
	CC_MD5_CTX _context;
	bool _hasBeenFinalized;
}

- (instancetype) init {
	if ((self = [super init])) {
		CC_MD5_Init(&_context);
		_queue = dispatch_queue_create("MD5 queue", DISPATCH_QUEUE_SERIAL);
	}
	return self;
}

- (void) updateWithBytes:(void const *_Nonnull)bytesPtr length:(NSUInteger)numBytes {
	NSAssert(_hasBeenFinalized == false, @"It's not valid to update a context that has already been finalized!");
	dispatch_sync(_queue, ^{
		//FIXME: CC_LONG is technically not equivalent to NSUInteger. This should update in chunks until numBytes is exhausted.
		CC_MD5_Update(&_context, bytesPtr, numBytes);
	});
}
- (void) finalizeAndGetDigestBytes:(unsigned char *_Nonnull)outBytesPtr {
	NSAssert(_hasBeenFinalized == false, @"It's not valid to finalize a context that has already been finalized!");
	dispatch_sync(_queue, ^{
		CC_MD5_Final(outBytesPtr, &_context);
	});
}

- (void) peekAtDigestBytes:(unsigned char *_Nonnull)outBytesPtr {
	NSAssert(_hasBeenFinalized == false, @"There's nothing to peek at; the context has already been finalized!");
	dispatch_sync(_queue, ^{
		CC_MD5_CTX tempContext;
		memcpy(&tempContext, &_context, sizeof(_context));
		CC_MD5_Final(outBytesPtr, &tempContext);
	});
}

- (NSString *_Nonnull) stringDescribingDigestBytes:(unsigned char *const _Nonnull)bytesPtr {
	return [NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x",
		 bytesPtr[0],  bytesPtr[1],  bytesPtr[2],  bytesPtr[3],
		 bytesPtr[4],  bytesPtr[5],  bytesPtr[6],  bytesPtr[7],
		 bytesPtr[8],  bytesPtr[9], bytesPtr[10], bytesPtr[11],
		bytesPtr[12], bytesPtr[13], bytesPtr[14], bytesPtr[15]
	];
}

@end
