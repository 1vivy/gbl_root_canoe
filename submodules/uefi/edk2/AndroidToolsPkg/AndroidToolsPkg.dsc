#/** @file
#  AndroidToolsPkg platform description. Builds the standalone RebootTools and
#  ArbTools UEFI applications plus the shared AndroidToolsUi menu library. The
#  package is self-contained: it ports the r32 DeviceInfo and reboot/recovery
#  code it needs and only relies on the standard EDK2 base packages for library
#  classes, so it does not depend on the stripped QcomModulePkg build config.
#
#  Build with, for example:
#    build -p AndroidToolsPkg/AndroidToolsPkg.dsc -a AARCH64 -b RELEASE
#
#  Copyright (c) 2026, contributors to the canoe ABL tree.
#  SPDX-License-Identifier: BSD-3-Clause
#**/

################################################################################
#
# Defines Section
#
################################################################################
[Defines]
  PLATFORM_NAME                  = AndroidToolsPkg
  PLATFORM_GUID                  = 3C9E7B14-2A48-4D6F-B1E5-7A0C91D8F2B3
  PLATFORM_VERSION               = 0.1
  DSC_SPECIFICATION              = 0x00010005
  OUTPUT_DIRECTORY               = Build/AndroidToolsPkg
  SUPPORTED_ARCHITECTURES        = ARM|AARCH64
  BUILD_TARGETS                  = DEBUG|RELEASE
  SKUID_IDENTIFIER               = DEFAULT

################################################################################
#
# Library Class mappings - standard EDK2 implementations only.
#
################################################################################
[LibraryClasses]
  UefiApplicationEntryPoint|MdePkg/Library/UefiApplicationEntryPoint/UefiApplicationEntryPoint.inf
  UefiBootServicesTableLib|MdePkg/Library/UefiBootServicesTableLib/UefiBootServicesTableLib.inf
  UefiRuntimeServicesTableLib|MdePkg/Library/UefiRuntimeServicesTableLib/UefiRuntimeServicesTableLib.inf
  UefiLib|MdePkg/Library/UefiLib/UefiLib.inf
  BaseLib|MdePkg/Library/BaseLib/BaseLib.inf
  BaseMemoryLib|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  BaseMemoryLibOptDxe|MdePkg/Library/BaseMemoryLib/BaseMemoryLib.inf
  MemoryAllocationLib|MdePkg/Library/UefiMemoryAllocationLib/UefiMemoryAllocationLib.inf
  DebugLib|MdePkg/Library/UefiDebugLibConOut/UefiDebugLibConOut.inf
  DebugPrintErrorLevelLib|MdePkg/Library/BaseDebugPrintErrorLevelLib/BaseDebugPrintErrorLevelLib.inf
  PrintLib|MdePkg/Library/BasePrintLib/BasePrintLib.inf
  DevicePathLib|MdePkg/Library/UefiDevicePathLib/UefiDevicePathLib.inf
  PcdLib|MdePkg/Library/BasePcdLibNull/BasePcdLibNull.inf
  AndroidToolsUi|AndroidToolsPkg/Library/AndroidToolsUi/AndroidToolsUi.inf

[LibraryClasses.ARM]
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

[LibraryClasses.AARCH64]
  ArmLib|ArmPkg/Library/ArmLib/ArmBaseLib.inf
  NULL|ArmPkg/Library/CompilerIntrinsicsLib/CompilerIntrinsicsLib.inf

################################################################################
#
# Build Options
#
# Mirror QcomModulePkg.dsc's link flags exactly. QcomModulePkg.dsc sets two
# DLINK_FLAGS lines that EDK2 concatenates:
#   1) -Wl,-Ttext=0x0            -> link the image at VMA 0 so the old CLANG35
#      GenFw resolves R_AARCH64_RELATIVE (.data pointer tables such as the menu
#      Items[] arrays) and GOT relocations against the load base. Without it,
#      GenFw leaves .data relative pointers zeroed and every menu item deref
#      faults (the menu renders only ">").
#   2) $(CLANG_EXTRA_DLINK_FLAGS) -> the makefile exports this as
#      "-Wl,--no-relax -Wl,--apply-dynamic-relocs" for clang >= 17 (we ship
#      clang 21). --no-relax stops lld from collapsing adrp+ldr(:got:) into
#      adr+nop in this small binary; the old GenFw cannot represent the relaxed
#      adr and mis-converts it to a dead "adrp xzr, ..." + garbage add, which
#      data-aborts on launch (Synchronous Exception). --apply-dynamic-relocs
#      writes the RELA addends into .data at link time as a second guarantee.
# QcomModulePkg's global data links and runs correctly with exactly these flags,
# so AndroidToolsPkg uses the same two lines. The flags are picked up when the
# package is built via the makefile "tools" target, which exports
# CLANG_EXTRA_DLINK_FLAGS the same way it does for the main ABL build.
#
################################################################################
[BuildOptions]
  *_CLANG35_AARCH64_DLINK_FLAGS = -Wl,-Ttext=0x0
  *_CLANG35_AARCH64_DLINK_FLAGS = $(CLANG_EXTRA_DLINK_FLAGS)

################################################################################
#
# Components - the shared menu library is pulled in transitively by the apps,
# so only the two applications need to be listed.
#
################################################################################
[Components.common]
  AndroidToolsPkg/Application/RebootTools/RebootTools.inf
  AndroidToolsPkg/Application/ArbTools/ArbTools.inf
