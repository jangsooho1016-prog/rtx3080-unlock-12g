#pragma once
/*
 * Board identity shared by the read-only probe and the unlock app.
 *
 * Target: MSI GeForce RTX 3080 SUPRIM X 12G LHR
 *   - GA102 (RTX 3080 12GB), PCI device 10de:220a
 *   - 12 GiB GDDR6X, 384-bit
 *
 * Only the values that are publicly fixed for this SKU live here. Everything
 * that differs per physical card or per PC (subsystem ID, BAR0 placement,
 * VBIOS/PROM hash, cold-state fuse/selector values) is measured by the probe
 * and written to board_measured.h by tools/make_board_profile.py.
 */
#ifdef GU_BOARD_TEST_FIXTURE
#include "tests/board_fixture_3080_10g.h"
#else
#define GU_BOARD_NAME "MSI RTX 3080 SUPRIM X 12G LHR"
#define GU_BOARD_ID 0x220a10deU            /* 10de:220a, RTX 3080 12GB */
#define GU_BOARD_SUBSYSTEM_VENDOR 0x1462U  /* MSI */
#define GU_BOARD_EXPECTED_FB_MB 0x3000U    /* 12288 MiB; probe must confirm */
#endif
