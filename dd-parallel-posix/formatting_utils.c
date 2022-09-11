//
//  formatting_utils.c
//  dd-parallel-posix
//
//  Created by Peter Hosey on 2022-08-28.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#include "formatting_utils.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

///Similar API to strlcpy/strlcat. Takes a buffer, value to append, and capacity (including null terminator); returns total length of new string.
size_t copyByteCountPhrase(char *const _Nonnull dst, unsigned long long const numBytes, size_t const dstCapacity) {
	enum { numUnits = 9 };
	static double const unitFactors[numUnits] = {
		1.0,

		1024.0,
		1024.0 * 1024.0,
		1024.0 * 1024.0 * 1024.0,
		1024.0 * 1024.0 * 1024.0 * 1024.0,

		1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0,
		1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0,
		1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0,
		1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0 * 1024.0,
	};
	static char const unitStrings[numUnits][6] = {
		"bytes",
		"KiB", "MiB", "GiB", "TiB",
		"PiB", "EiB", "YiB", "ZiB",
	};
	unsigned int unitIdx = numUnits - 1;
	for (; unitIdx > 0 && numBytes < unitFactors[unitIdx]; --unitIdx);

	double const numThisUnit = numBytes / (double)unitFactors[unitIdx];
	double remainingBytes = fmod(numBytes, unitFactors[unitIdx]);
	size_t totalLen =
		remainingBytes > 0.0
		? snprintf(dst, dstCapacity, "%.2f %s", numThisUnit, unitStrings[unitIdx])
		: snprintf(dst, dstCapacity, "%llu %s", numBytes / (unsigned long long)unitFactors[unitIdx], unitStrings[unitIdx]);

	return totalLen;
}
size_t copyIntervalPhrase(char *const _Nonnull dst, double const numSeconds, size_t const dstCapacity) {
	enum { numUnits = 5 };
	static double const unitFactors[numUnits] = {
		1.0 / 1000.0,
		1.0,

		60.0,
		60.0 * 60.0,
		60.0 * 60.0 * 24.0,
	};
	static char const unitStrings[numUnits][6] = {
		"ms",
		"sec",
		"min", "hr", "d",
	};
	unsigned int unitIdx = numUnits - 1;
	for (; unitIdx > 0 && numSeconds < unitFactors[unitIdx]; --unitIdx);

	char *dstptr = dst;
	size_t totalLen = 0;
	unsigned int numThisUnit = numSeconds / unitFactors[unitIdx];
	double remainingSeconds = fmod(numSeconds, unitFactors[unitIdx]);
	size_t len = snprintf(dstptr, dstCapacity - totalLen, "%u %s", (unsigned int)numThisUnit, unitStrings[unitIdx]);
	totalLen += len;
	while (remainingSeconds > 0.0 && unitIdx > 0) {
		if (unitIdx == 0 && totalLen > 0) break; //Don't use ms if we've already used other units.
		if (totalLen >= dstCapacity) break;
		dstptr += len;
		if (len > 0) {
			*dstptr++ = ' ';
			++totalLen;
		}
		*dstptr = '\0';
		if (totalLen >= dstCapacity) break;

		numThisUnit = remainingSeconds / unitFactors[unitIdx];
		remainingSeconds = fmod(remainingSeconds, unitFactors[unitIdx]);
		if (numThisUnit == 0) { //e.g., 1 hr 3 sec
			len = 0;
		} else {
			len = snprintf(dstptr, dstCapacity - totalLen, "%u %s", numThisUnit, unitStrings[unitIdx]);
			totalLen += len;
		}
		--unitIdx;
	}

	return totalLen;
}
