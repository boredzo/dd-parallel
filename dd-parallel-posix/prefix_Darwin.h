//
//  prefix-Darwin.h
//  dd-parallel
//
//  Created by Peter Hosey on 2022-09-01.
//  Copyright © 2022 Peter Hosey. All rights reserved.
//

#ifndef prefix_Darwin_h
#define prefix_Darwin_h

#include <sys/types.h>
#include <stdbool.h>
#include <sys/syslimits.h>
#include <sys/errno.h>
#include <sysexits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>
#include <stdio.h>

#define CLOCK_THEGOODONE CLOCK_UPTIME_RAW

#define pthread_setname_self(name) pthread_setname_np((name))

#define EXISTS_F_RDAHEAD 1
#define EXISTS_F_NOCACHE 1

#endif /* prefix_Darwin_h */
