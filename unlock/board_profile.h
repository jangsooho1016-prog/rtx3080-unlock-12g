#pragma once
/*
 * Full board profile for the unlock app.
 *
 * The original project pinned every check to one MSI RTX 3080 10GB
 * (10de:2206 / 1462:3890, VBIOS 94.02.59.00.aa) in one specific PC. This port
 * targets the RTX 3080 SUPRIM X 12G LHR, but values that can only be known by
 * reading the actual card are NOT guessed: they come from board_measured.h,
 * generated from a probe run on the target machine:
 *
 *   python tools/make_board_profile.py --report <USB>/RTX3080-PROBE/RUNxx/REPORT.TXT \
 *          --prom <USB>/RTX3080-PROBE/RUNxx/PROM.BIN --output unlock/board_measured.h
 *
 * Without that file the unlock app refuses to compile.
 */
#ifdef GU_BOARD_TEST_FIXTURE
/* Unit tests replay the original 10GB card's evidence. */
#include "tests/board_fixture_3080_10g.h"
#else
#include "board_ids.h"

#if defined(__has_include)
#if __has_include("board_measured.h")
#include "board_measured.h"
#endif
#endif

#ifndef GU_BOARD_MEASURED
#error "board_measured.h is missing: run the read-only probe on the target PC, then tools/make_board_profile.py (see docs/PORT-3080-12G.md)."
#else

#if GU_BOARD_MEASURED_ID != GU_BOARD_ID
#error "board_measured.h was generated from a different GPU than board_ids.h targets."
#endif

/* Values required from board_measured.h. */
#if !defined(GU_BOARD_SUBSYSTEM) || !defined(GU_BOARD_CHIP) ||          \
    !defined(GU_BOARD_CLASS_REV) || !defined(GU_BOARD_CFG_C8) ||          \
    !defined(GU_BOARD_BAR0) || !defined(GU_BOARD_BAR0_LEN) ||            \
    !defined(GU_BOARD_COLD_FEAT_PLM) || !defined(GU_BOARD_COLD_SS0) ||   \
    !defined(GU_BOARD_FB_MB) || !defined(GU_BOARD_PROM_SHA256)
#error "board_measured.h is incomplete; regenerate it with tools/make_board_profile.py."
#endif

#if (GU_BOARD_SUBSYSTEM & 0xffffU) != GU_BOARD_SUBSYSTEM_VENDOR
#error "Measured subsystem vendor is not MSI (1462)."
#endif
#endif /* GU_BOARD_MEASURED */
#endif /* GU_BOARD_TEST_FIXTURE */
