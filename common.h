#ifndef COMMON_H
#define COMMON_H
#include "errno.h"
#include <stdio.h>

// macro to simplify error handling
#define GENERIC_ERROR_HELPER(cond, errCode, msg) do {               \
        if (cond) {                                                 \
            fprintf(stderr, "%s: %s\n", msg, strerror(errCode));    \
            exit(EXIT_FAILURE);                                     \
        }                                                           \
    } while(0)

#define ERROR_HELPER(ret, msg)          GENERIC_ERROR_HELPER((ret < 0), errno, msg)
#define PTHREAD_ERROR_HELPER(ret, msg)  GENERIC_ERROR_HELPER((ret != 0), ret, msg)

/**
#define debug_printf(fmt, ...) \
           do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)
**/

#define debug_print(fmt, ...) \
            do { if (DEBUG) fprintf(stderr, fmt, ##__VA_ARGS__); } while (0)

/* Configuration parameters */
#define SERVER_ADDRESS  "127.0.0.1"
#define WORLDSIZE 512
#define DEBUG 1
#define BUFFERSIZE 1000000
#define UDPPORT 8888
#define HIDE_RANGE 6
#define AFK_RANGE 1
#define MAX_AFK_COUNTER 6
#define MAX_TIME_WITHOUT_VEHICLEUPDATE 10
#define MAX_TIME_WITHOUT_WORLDUPDATE 10
#endif

