//
//  prefix_Linux.h
//  dd-parallel
//
//  Created by Peter Hosey on 2022-09-01.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#ifndef prefix_Linux_h
#define prefix_Linux_h

#define _GNU_SOURCE 1

#include <sys/types.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/errno.h>
#include <sysexits.h>
#include <stdlib.h>
#include <string.h>
#if __has_include(<bsd/string.h>)
#	include <bsd/string.h>
#else
#	error This program requires libbsd. You should be able to install it from your favorite package manager as libbsd-dev.
#endif
#include <unistd.h>
#include <time.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>

#define CLOCK_THEGOODONE CLOCK_MONOTONIC_RAW
#ifndef PTHREAD_ERRORCHECK_MUTEX_INITIALIZER
#	define PTHREAD_ERRORCHECK_MUTEX_INITIALIZER PTHREAD_ERRORCHECK_MUTEX_INITIALIZER_NP
#endif
#ifndef SIGINFO
#	define SIGINFO SIGUSR1
#endif

#endif /* prefix_Linux_h */
