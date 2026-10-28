/**
 * @file    anchor_selftest.h
 * @brief   End-to-end Anchor Protocol test with the real TROPIC01 and a software TEST shield.
 *
 * TEST ONLY. The "shield" here is a fixed private key compiled into the firmware; a real shield
 * signs inside its own TROPIC01. The output (JSON) is checked on a PC with tests/host/verify.js.
 *
 * Shared by phase 7 (on the CiWi) and tests/host (on a PC, against the TROPIC01 model).
 */
#ifndef ANCHOR_SELFTEST_H
#define ANCHOR_SELFTEST_H

#include <stdbool.h>

#include "tr01_ciwi.h"

typedef struct {
    /** Receives the JSON text, piece by piece. */
    void (*emit)(const char *text);
    /** Show what is about to be signed; return true when the owner approves (button). */
    bool (*confirm)(const char *title, const char *detail);
    /** Called for every check. */
    void (*result)(const char *name, bool ok);
} anchor_selftest_io_t;

/** Returns the number of failed checks (0 = all passed). Uses one claim counter value. */
int anchor_selftest_run(ciwi_tr01_t *t, const anchor_selftest_io_t *io);

#endif /* ANCHOR_SELFTEST_H */
