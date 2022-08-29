//
//  PRHProgressReporter.h
//  dd-parallel
//
//  Created by Peter Hosey on 2022-08-21.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#import <Foundation/Foundation.h>

@interface PRHProgressReporter : NSObject

//Must be a time interval since reference date.
@property NSTimeInterval whenCopyingStarted;

- (void) recordProgressAsOfInstant:(NSTimeInterval)tisrd
	cumulativeBytesCopied:(unsigned long long)bytesCopiedSoFar;

- (void) reportProgressAsOfInstant:(NSTimeInterval)tisrd
	cumulativeBytesCopied:(unsigned long long)bytesCopiedSoFar
	isFinal:(bool)isFinal;

@end
