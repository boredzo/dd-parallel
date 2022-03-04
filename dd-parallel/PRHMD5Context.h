//
//  PRHMD5Context.h
//  dd-parallel
//
//  Created by Peter Hosey on 2019-12-12.
//  Copyright © 2019 Peter Hosey. All rights reserved.
//

#import <Foundation/Foundation.h>

enum {
	//CC_MD5_DIGEST_LENGTH
	PRHMD5DigestNumberOfBytes = 16
};

@interface PRHMD5Context : NSObject

- (void) updateWithBytes:(void const *_Nonnull)bytesPtr length:(NSUInteger)numBytes;
- (void) finalizeAndGetDigestBytes:(unsigned char *_Nonnull)outBytesPtr;

//Like finalize but works on a copy of the internal context so as not to prematurely end hashing.
- (void) peekAtDigestBytes:(unsigned char *_Nonnull)outBytesPtr;


@end
