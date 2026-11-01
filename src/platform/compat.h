#pragma once
#if defined(__GNUC__) && defined(_WIN32)
#include <cpuid.h>
#ifndef TCP_KEEPIDLE
#define TCP_KEEPIDLE 3
#define TCP_KEEPCNT 16
#define TCP_KEEPINTVL 17
#endif
#endif
