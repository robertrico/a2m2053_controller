#pragma once
#include <stdio.h>
static int t_run, t_fail;
#define CHECK(cond) do { t_run++; if (!(cond)) { t_fail++; \
    printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define TEST_END() do { printf("%s: %d checks, %d failed\n", __FILE__, t_run, t_fail); \
    return t_fail ? 1 : 0; } while (0)
