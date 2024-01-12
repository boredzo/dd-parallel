//
//  formatting_utils.h
//  dd-parallel-posix
//
//  Created by Peter Hosey on 2022-08-28.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#ifndef formatting_utils_h
#define formatting_utils_h

#include <sys/types.h>
#include <stdbool.h>

size_t copyByteCountPhrase(char *const _Nonnull dst, unsigned long long const numBytes, size_t const dstCapacity);
size_t copyIntervalPhrase(char *const _Nonnull dst, double const numSeconds, size_t const dstCapacity);

#endif /* formatting_utils_h */
