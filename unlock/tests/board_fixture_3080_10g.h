#pragma once
/* Simulated-hardware fixture for the unit tests only.
 * The test suites replay the original MSI RTX 3080 10GB evidence
 * (evidence/boot-03), so they keep that card's identity and cold state.
 * Never used by build.ps1 / BOOTX64.EFI. */
#define GU_BOARD_NAME "MSI RTX 3080 10GB (test fixture)"
#define GU_BOARD_ID 0x220610deU
#define GU_BOARD_SUBSYSTEM_VENDOR 0x1462U
#define GU_BOARD_EXPECTED_FB_MB 0x2800U
#define GU_BOARD_MEASURED 1
#define GU_BOARD_MEASURED_ID 0x220610deU
#define GU_BOARD_SUBSYSTEM 0x38901462U
#define GU_BOARD_CLASS_REV 0x030000a1U
#define GU_BOARD_CFG_C8 0x00050011U
#define GU_BOARD_BAR0 0x84000000U
#define GU_BOARD_BAR0_LEN 0x01000000U
#define GU_BOARD_CHIP 0xb72000a1U
#define GU_BOARD_COLD_FEAT_PLM 0xffffff8fU
#define GU_BOARD_COLD_SS0 0x46254040U
#define GU_BOARD_FB_MB 0x00002800U
/* The 10GB evidence showed SS1=2 or 3 when cold; upstream SS1=8 policy. */
#define GU_BOARD_COLD_SS1 0x3U
#define GU_BOARD_COLD_SS1_ALT 0x2U
#define GU_SS1_FULL 1
#define GU_BOARD_PROM_SHA256 "f3dc8f07b0b8c9856d192df172dfa7f7f84ac17ef6edfdb448ad7fa94efd2789"
