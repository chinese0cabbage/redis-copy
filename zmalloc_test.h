#pragma once

#include "zmalloc.h"

void zmalloc_test(){
    const char s[] = "123456";
    char *w = s + 2; 
    RLOG("111%c", w[-1]);
}