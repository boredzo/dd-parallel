//
//  PRHProgressReporter.m
//  dd-parallel
//
//  Created by Peter Hosey on 2022-08-21.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#import "PRHProgressReporter.h"

@implementation PRHProgressReporter
{
	NSByteCountFormatter *_byteCountForTotalFmtr;
	NSByteCountFormatter *_byteCountForRateFmtr;
	NSDateComponentsFormatter *_dateComponentsFmtr;
}

- (instancetype)init {
	if ((self = [super init])) {
		_byteCountForTotalFmtr = [NSByteCountFormatter new];
		_byteCountForTotalFmtr.formattingContext = NSFormattingContextStandalone;
		_byteCountForTotalFmtr.includesActualByteCount = true;
		_byteCountForRateFmtr = [NSByteCountFormatter new];
		_byteCountForRateFmtr.formattingContext = NSFormattingContextStandalone;

		_dateComponentsFmtr = [NSDateComponentsFormatter new];
		_dateComponentsFmtr.maximumUnitCount = 3;
		_dateComponentsFmtr.unitsStyle = NSDateComponentsFormatterUnitsStyleShort;
	}
	return self;
}

- (void) reportProgressAsOfInstant:(NSTimeInterval)tisrd
	cumulativeBytesCopied:(unsigned long long)bytesCopiedSoFar
	isFinal:(bool)isFinal
{
	char *const fmtString = isFinal
		? "Bytes copied: %s.\nFinal time: %s.\nFinal rate: %s per second.\n"
	: "Bytes copied so far: %s.\nTime so far: %s.\nCurrent rate: %s per second.\n";
	NSTimeInterval const numSecondsElapsed = tisrd - _whenCopyingStarted;
	fprintf(stderr, fmtString,
		[_byteCountForTotalFmtr stringFromByteCount:bytesCopiedSoFar].UTF8String,
		[_dateComponentsFmtr stringFromTimeInterval:numSecondsElapsed].UTF8String,
		[_byteCountForRateFmtr stringFromByteCount:bytesCopiedSoFar / numSecondsElapsed].UTF8String
	);
}

@end
