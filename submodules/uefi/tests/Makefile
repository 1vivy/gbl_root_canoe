include ../../../version.mk

# SuperFbMenu.h refuses to compile without an injected version, and it arrives
# through the hook headers rather than only where a test names it. Appending to
# CFLAGS stamps every host test, so a new recipe cannot omit it and fail only on
# a clean tree, where nothing is left over from a previous build to hide it.
SFB_VERSION_CFLAGS := -DSFB_BDS_VERSION='"$(CANOE_VERSION)"'

CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror
CFLAGS += $(SFB_VERSION_CFLAGS)

.PHONY: test test_gpt_slot_reset clean

test: test_profile test_deviceinfo test_devinfo test_slot_attributes test_slot_retries test_gpt_slot_reset test_last_launch test_boot_root test_tzmap test_fat test_hooks test_launch test_config test_bls test_linuxboot test_log test_surface
	./test_profile
	./test_deviceinfo
	./test_devinfo
	./test_slot_attributes
	./test_slot_retries
	./test_last_launch
	./test_boot_root
	./test_tzmap
	./test_fat
	./test_hooks
	./test_launch
	./test_config
	./test_bls
	./test_linuxboot
	./test_log
	./test_surface

test_profile: test_profile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

test_deviceinfo: test_deviceinfo.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbDeviceInfo.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

test_devinfo: test_devinfo.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbDevInfo.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -fshort-wchar $(HOOK_INCLUDES) -o $@ $^

test_slot_attributes: test_slot_attributes.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlotAttributes.h
	$(CC) $(CFLAGS) -DMDEPKG_NDEBUG -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

test_slot_retries: test_slot_retries.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlots.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlots.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlotRetry.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbSlotAttributes.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbGptName.h
	$(CC) $(CFLAGS) -DMDEPKG_NDEBUG -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $^

test_gpt_slot_reset:
	./test_gpt_slot_reset.sh

test_last_launch: \
	../edk2/QcomModulePkg/Application/LinuxLoader/TestLastLaunch.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLastLaunch.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -I../edk2/QcomModulePkg/Application/LinuxLoader -o $@ $^

test_boot_root: \
	../edk2/QcomModulePkg/Application/LinuxLoader/TestBootRoot.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBootRoot.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -I../edk2/QcomModulePkg/Application/LinuxLoader -o $@ $^

# Pure canoe.cfg parser: no EDK2 types at all, so it builds against stdint.
test_config: test_config.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbConfig.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

# Pure Boot Loader Specification Type #1 parser: same shape as test_config,
# no EDK2 types, so it builds against stdint.
test_bls: test_bls.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbBls.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

HOOK_DIR := ../edk2/QcomModulePkg/Application/LinuxLoader/Hook
HOOK_INCLUDES := \
	-I../edk2/MdePkg/Include \
	-I../edk2/MdePkg/Include/X64 \
	-I../edk2/QcomModulePkg/Include \
	-I../edk2/QcomModulePkg/Application/LinuxLoader \
	-I$(HOOK_DIR)

test_hooks: test_hooks.c \
	$(HOOK_DIR)/InstallAll.c \
	$(HOOK_DIR)/VerifiedBootHook.c \
	$(HOOK_DIR)/QseecomHook.c \
	$(HOOK_DIR)/SpssHook.c \
	$(HOOK_DIR)/ScmHook.c \
	$(HOOK_DIR)/BlockIoHook.c \
	$(HOOK_DIR)/SuperFbProfile.c \
	$(HOOK_DIR)/SuperFbDeviceInfo.c \
	$(HOOK_DIR)/SuperFbTzMap.c \
	$(HOOK_DIR)/SuperFbProfileRewrite.c \
	$(HOOK_DIR)/HookCommon.h
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $^

test_tzmap: test_tzmap.c $(HOOK_DIR)/SuperFbTzMap.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

# Pure volume classifier: header-only, so no production .c source is linked.
# Needs the EDK2 include path because it keeps the real UEFI types.
test_fat: test_fat.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbFatClassify.h
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

test_launch: test_launch.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbMenu.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

# Initrd and DTB publication. Links the real SuperFbLinuxBoot.c against a fake
# gBS so the GUIDs, the device-path shape and the LoadFile2 size contract are
# checked against the values the kernel actually looks for.
test_linuxboot: test_linuxboot.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLinuxBoot.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLinuxBoot.h
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $(filter %.c,$^)

# EDK2's x64 varargs helpers assume the firmware ABI; use the host ABI here.
test_log: test_log.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLog.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLogRing.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLog.h
	$(CC) $(CFLAGS) -DNO_MSABI_VA_FUNCS -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $(filter %.c,$^)

test_surface: test_surface.c \
  ../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfaceModel.c \
  ../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfaceModel.h \
  ../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfacePolicy.c \
  ../edk2/AndroidToolsPkg/Application/SurfaceTools/SurfacePolicy.h
	$(CC) $(CFLAGS) -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $(filter %.c,$^)

clean:
	rm -f test_profile test_deviceinfo test_devinfo test_slot_attributes test_slot_retries test_last_launch test_tzmap test_fat test_hooks test_launch test_config test_bls test_linuxboot test_log test_surface

.PHONY: test_image_map
test: test_image_map
test_image_map:
	$(CC) $(CFLAGS) -DMDEPKG_NDEBUG -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -I../edk2/EmbeddedPkg/Include -I../edk2/Ext4Pkg/Include -o test_image_map test_image_map.c ../edk2/Ext4Pkg/Ext4Dxe/ImageMap.c
	./test_image_map
