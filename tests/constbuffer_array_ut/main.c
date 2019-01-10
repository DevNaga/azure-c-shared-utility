// Copyright (c) Microsoft. All rights reserved.

#include <stdarg.h>
#include "testrunnerswitcher.h"

char* construct_args_format()
{
    return "%d %s";
}
void log_args(char* format, va_list args_va)
{
    (void)vprintf(format, args_va);
}
void a_to_be_coded(va_list args_va, int arg1, char* arg2);
void a_with_dummy(int arg1, char* arg2, int dummy, ...)
{
    va_list va;
    va_start(va, dummy);
    a_to_be_coded(va, arg1, arg2);
    va_end(va);
}
void a(int arg1, char* arg2)
{
    a_with_dummy(arg1, arg2, 0, arg1, arg2);
}
void a_to_be_coded(va_list args_va, int arg1, char* arg2)
{
    (void)arg1;
    (void)arg2;
    log_args(construct_args_format(), args_va);
}

int main(void)
{
    size_t failedTestCount = 0;
    RUN_TEST_SUITE(constbuffer_array_unittests, failedTestCount);

#ifdef VLD_OPT_REPORT_TO_STDOUT
    failedTestCount = VLDGetLeaksCount() > 0 ? 1 : 0;
#endif

    return failedTestCount;
}
