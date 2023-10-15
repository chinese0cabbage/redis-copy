#pragma once

#include <stdio.h>
#include <time.h>
#include <malloc.h>
#include <sys/time.h>
#include <string.h>

static char szTime[19];

static void logTime(void)
{
    struct timeval tmv;
    if(gettimeofday(&tmv, NULL) < 0){
        printf("get time error\n");
        return;
    }
    struct tm *tmr = localtime(&tmv.tv_sec);
    char us[6] = {0};
    sprintf(us, "%lu", tmv.tv_usec);
    memcpy(szTime + strftime(szTime, 18, "%m-%d %H:%M:%S.", tmr), us, 6);
}

#define RLOG(format, ...) logTime(); printf("%s %s:%d] " format "\n", szTime, __FILE__, __LINE__, ##__VA_ARGS__)