# Builds the READ-ONLY probe as probe/build/BOOTX64.EFI.
# The probe reads PCI config, a few GPU status registers and the 1 MiB PROM
# window, and writes REPORT.TXT / PROM.BIN to <USB>\RTX3080-PROBE\RUNxx.
# It performs no GPU writes or resets.
$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath (Split-Path $PSScriptRoot -Parent)
$llvmRoot = 'C:/Program Files (x86)/Microsoft Visual Studio/2019/BuildTools/VC/Tools/Llvm/x64/bin'
New-Item -ItemType Directory -Path 'probe/build' -Force | Out-Null
$objects = @()
foreach ($source in @('probe_app','probe_core','probe_efi_rt')) {
    $object = "probe/build/$source.obj"
    & "$llvmRoot/clang.exe" -target x86_64-pc-windows-msvc -std=c11 -ffreestanding -fno-builtin -fno-stack-protector -mno-red-zone -fshort-wchar -Os -Wall -Wextra -Werror -Wno-unused-parameter -I deps/edk2/MdePkg/Include -I deps/edk2/MdePkg/Include/X64 -c "$source.c" -o $object
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
    $objects += $object
}
& "$llvmRoot/lld-link.exe" /nologo /nodefaultlib /subsystem:efi_application /entry:efi_main /machine:x64 /base:0 /dynamicbase /nxcompat /timestamp:0 /opt:ref /opt:icf /out:probe/build/BOOTX64.EFI @objects
if ($LASTEXITCODE -ne 0) { throw 'Probe link failed' }
Write-Output 'Built probe/build/BOOTX64.EFI (read-only). Copy it to <USB>\EFI\BOOT\BOOTX64.EFI and create an empty folder <USB>\RTX3080-PROBE.'
