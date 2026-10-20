#pragma once
#include <Uefi.h>
#include <Protocol/DevicePath.h>
/*
 * Windows Boot Manager chainload.
 *
 * Preload: before arming, find exactly one non-USB file system that holds
 * \EFI\Microsoft\Boot\bootmgfw.efi and LoadImage it without starting it.
 * Start: only after a clean, fully closed transaction. Every other path
 * discards (UnloadImage) the preloaded image and returns to firmware as before.
 *
 * This module never calls ConnectController and never touches the GPU.
 */
#define GU_CHAIN_FILE L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi"
#define GU_CHAIN_MAX_FS 64
#define GU_CHAIN_WATCHDOG_SECONDS 300
typedef struct {
  EFI_HANDLE image;     /* loaded, not started; NULL when nothing is pending */
  EFI_HANDLE device;    /* file system handle that holds bootmgfw.efi */
  UINTN scanned, skipped_self, skipped_usb, skipped_path, candidates;
  EFI_STATUS status;    /* preload result */
} GU_CHAIN;
EFI_STATUS GuChainPreload(EFI_BOOT_SERVICES *Bs, EFI_HANDLE Self, GU_CHAIN *Chain);
EFI_STATUS GuChainDiscard(EFI_BOOT_SERVICES *Bs, GU_CHAIN *Chain);
EFI_STATUS GuChainStart(EFI_BOOT_SERVICES *Bs, GU_CHAIN *Chain);
