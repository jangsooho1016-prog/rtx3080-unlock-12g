#pragma once

#include "port.h"

typedef struct {
  UINT16 pm, pcie, msi, msix;
  UINT8 pcie_version;
} GU_CAPS;

/* Config is a complete, CPU-owned 256-byte snapshot. Out is cleared on failure. */
BOOLEAN GuCheckConfig(const UINT32 Config[64], UINTN Segment, UINTN Bus,
                      UINTN Device, UINTN Function, GU_CAPS *Out);

/* Path must point to readable memory through its terminator, up to MaxBytes.
   MaxBytes is a read bound, not necessarily the exact allocation length.
   Returns 1 for the Intel route, 2 for the RTX route, and 0 otherwise. */
int GuDisplayPath(const EFI_DEVICE_PATH_PROTOCOL *Path, UINTN MaxBytes);

/* One OpenProtocolInformation entry on the target handle. */
typedef struct {
  UINT32 Attributes, OpenCount;
  EFI_HANDLE Agent, Controller;
  BOOLEAN PciIo;
} GU_OPEN_RECORD;

#define GU_MAX_TARGET_CHILDREN 4U

/* Release policy for the firmware driver bound to Target. Accepts only passive
   opens, exactly one PciIo BY_DRIVER open by one agent on Target, and
   BY_CHILD_CONTROLLER opens by that same agent for distinct non-Target child
   handles (deduplicated, at most GU_MAX_TARGET_CHILDREN). Anything else,
   including a second driver or an exclusive open, is EFI_ACCESS_DENIED.
   The caller must still prove every child is a target GOP output. */
EFI_STATUS GuReleasePolicy(const GU_OPEN_RECORD *Records, UINTN Count,
                           EFI_HANDLE Target, EFI_HANDLE *Agent,
                           EFI_HANDLE Children[GU_MAX_TARGET_CHILDREN],
                           UINTN *ChildCount);

/* Cold SS0 plausibility. The measured cold value varies by single bits
   between cold boots on the 12G card (0x40132104, 0x40132114, 0x42132104
   observed), so an exact match is not a reliable cold indicator. Accept a
   value only if no 4-bit selector has bit 3 set: the unlock value is
   0x88888888, so any partially or fully applied state is refused. PLM,
   WPR2, GFW and FB checks remain the authoritative cold-state gates. */
BOOLEAN GuColdSs0Plausible(UINT32 Ss0);
