CC ?= cc
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Werror

.PHONY: test clean

test: test_store_mode test_profile test_tzmap test_fat test_hooks test_launch
	./test_store_mode
	./test_profile
	./test_tzmap
	./test_fat
	./test_hooks
	./test_launch

test_store_mode: test_store_mode.c
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

test_profile: test_profile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.c
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
	$(HOOK_DIR)/SuperFbTzMap.c \
	$(HOOK_DIR)/SuperFbProfileRewrite.c
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $^

test_tzmap: test_tzmap.c $(HOOK_DIR)/SuperFbTzMap.c
	$(CC) $(CFLAGS) -DSFB_HOST_BUILD -o $@ $^

# Pure volume classifier: header-only, so no production .c source is linked.
# Needs the EDK2 include path because it keeps the real UEFI types.
test_fat: test_fat.c
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

test_launch: test_launch.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbLaunchPolicy.h \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbProfile.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/Hook/SuperFbManagedPath.c \
	../edk2/QcomModulePkg/Application/LinuxLoader/SuperFbEntries.c
	$(CC) $(CFLAGS) -Wno-unused-function -fno-builtin -fshort-wchar $(HOOK_INCLUDES) -o $@ $<

clean:
	rm -f test_store_mode test_profile test_tzmap test_fat test_hooks test_launch
