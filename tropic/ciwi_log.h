/**
 * @file    ciwi_log.h
 * @brief   One logging macro for code that runs both on the CiWi and on the host test bench.
 *
 * On the STM32 it prints through the debug UART (PA9, 115200 8N1).
 * On the host (CIWI_HOST defined) it prints to stdout.
 */
#ifndef CIWI_LOG_H
#define CIWI_LOG_H

#ifdef CIWI_HOST
#include <stdio.h>
#define CIWI_LOG(...) printf(__VA_ARGS__)
#else
#include "uart_debug.h"
#define CIWI_LOG(...) debug_printf(__VA_ARGS__)
#endif

/** Print a labelled hex string on one line. */
void ciwi_log_hex(const char *label, const unsigned char *buf, unsigned len);

#endif /* CIWI_LOG_H */
