//
//  PRHProgressReporter.m
//  dd-parallel
//
//  Created by Peter Hosey on 2022-08-21.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#import "PRHProgressReporter.h"

@interface PRHProgressMoment: NSObject
+ (instancetype) momentWithCumulativeByteCount:(unsigned long long)bytesCopiedSoFar instant:(NSTimeInterval)tisrd;
@property unsigned long long bytesCopiedSoFar;
@property NSTimeInterval instant;
@end

enum { maxNumberOfMomentsAtATime = 1000 };

@implementation PRHProgressReporter
{
	NSByteCountFormatter *_byteCountForTotalFmtr;
	NSByteCountFormatter *_byteCountForRateFmtr;
	NSDateComponentsFormatter *_dateComponentsFmtr;
	NSMutableArray *_recordedMoments;
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

		_recordedMoments = [NSMutableArray arrayWithCapacity:maxNumberOfMomentsAtATime];
	}
	return self;
}


- (void) recordProgressAsOfInstant:(NSTimeInterval)tisrd
	cumulativeBytesCopied:(unsigned long long)bytesCopiedSoFar
{
	PRHProgressMoment *_Nonnull const moment = [PRHProgressMoment momentWithCumulativeByteCount:bytesCopiedSoFar instant:tisrd];
	if (_recordedMoments.count >= maxNumberOfMomentsAtATime) {
		[_recordedMoments removeObjectAtIndex:0];
	}
	[_recordedMoments addObject:moment];
}

- (unsigned long long) averageBytesCopiedOverTimeWindow {
	PRHProgressMoment *_Nonnull const earliestMoment = [_recordedMoments firstObject];
	PRHProgressMoment *_Nonnull const latestMoment = [_recordedMoments lastObject];

	NSLog(@"Number of moments: %lu", _recordedMoments.count);
	if (earliestMoment == nil) {
		return 0;
	}
	NSTimeInterval const whenCopyingStarted = _whenCopyingStarted;
	NSLog(@"Earliest moment: %llu bytes as of %f seconds", earliestMoment.bytesCopiedSoFar, earliestMoment.instant - whenCopyingStarted);
	NSLog(@"  Latest moment: %llu bytes as of %f seconds", latestMoment.bytesCopiedSoFar, latestMoment.instant - whenCopyingStarted);

	unsigned long long const bytesCopiedThisWindow = latestMoment.bytesCopiedSoFar - earliestMoment.bytesCopiedSoFar;
	NSTimeInterval const secondsElapsedThisWindow = latestMoment.instant - earliestMoment.instant;

	//On the one hand, it's unfortunate that we have to truncate to an integer.
	//On the other hand, it's necessary because NSByteCountFormatter doesn't deal in fractional bytes.
	//On the third hand, it's OK because our byte counts *should* be big enough that the numbers of MB or GB are fractional anyway.
	return bytesCopiedThisWindow / secondsElapsedThisWindow;
}

- (void) reportProgressAsOfInstant:(NSTimeInterval)tisrd
	cumulativeBytesCopied:(unsigned long long)bytesCopiedSoFar
	isFinal:(bool)isFinal
{
	NSTimeInterval const numSecondsElapsed = tisrd - _whenCopyingStarted;
	char const *_Nonnull const bytesCopiedStr = [_byteCountForTotalFmtr stringFromByteCount:bytesCopiedSoFar].UTF8String;
	char const *_Nonnull const secondsElapsedStr = [_dateComponentsFmtr stringFromTimeInterval:numSecondsElapsed].UTF8String;
	char const *_Nonnull const overallCopyRateStr = [_byteCountForRateFmtr stringFromByteCount:bytesCopiedSoFar / numSecondsElapsed].UTF8String;
	if (! isFinal) {
		char const *_Nonnull const averageCopyRateStr = [_byteCountForRateFmtr stringFromByteCount:[self averageBytesCopiedOverTimeWindow]].UTF8String;
		fprintf(stderr, "Bytes copied so far: %s.\nTime so far: %s.\nCurrent rate: %s per second (overall average: %s per second).\n",
			bytesCopiedStr,
			secondsElapsedStr,
			averageCopyRateStr,
			overallCopyRateStr
		);
	} else {
		fprintf(stderr, "Bytes copied: %s.\nFinal time: %s.\nFinal rate: %s per second.\n",
			bytesCopiedStr,
			secondsElapsedStr,
			overallCopyRateStr
		);
	}
}

@end

@implementation PRHProgressMoment

+ (instancetype) momentWithCumulativeByteCount:(unsigned long long)bytesCopiedSoFar instant:(NSTimeInterval)tisrd {
	return [[self alloc] initWithCumulativeByteCount:bytesCopiedSoFar instant:tisrd];
}
- initWithCumulativeByteCount:(unsigned long long)bytesCopiedSoFar  instant:(NSTimeInterval)tisrd {
	if ((self = [super init])) {
		_bytesCopiedSoFar = bytesCopiedSoFar;
		_instant = tisrd;
	}
	return self;
}

@end
