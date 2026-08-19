// 从 Magisk v28.1 magiskboot 扩展的 vendor_boot 修补逻辑
// 集成进 magiskboot，直接调用 unpack/repack API + rust::cpio_commands，
// 不依赖外部 mboot 二进制。支持 vendor_boot header v4（vendor_ramdisk）。
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>
#include <strings.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/ioctl.h>

#include "magiskboot.hpp"
#include "boot-rs.hpp"

#define BUF_SIZE 4096

#define BLKFLSBUF 0x1261  /* _IO(0x12, 97) flush block device buffers */

// 复制 src 到 dst（普通文件，dst 截断）
static int copy_file(const char *src, const char *dst) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dfd < 0) { close(sfd); return -1; }
    char buf[65536];
    ssize_t n;
    int ret = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, (size_t) n) != n) { ret = -1; break; }
    }
    if (n < 0) ret = -1;
    close(sfd);
    close(dfd);
    return ret;
}

// 写 src 到块设备 blkdev，并 flush buffer（等价 dd conv=fsync）
static int write_to_block(const char *src, const char *blkdev) {
    int sfd = open(src, O_RDONLY);
    if (sfd < 0) return -1;
    int dfd = open(blkdev, O_WRONLY);
    if (dfd < 0) { close(sfd); return -1; }
    char buf[65536];
    ssize_t n;
    int ret = 0;
    while ((n = read(sfd, buf, sizeof(buf))) > 0) {
        if (write(dfd, buf, (size_t) n) != n) { ret = -1; break; }
    }
    if (n < 0) ret = -1;
    if (ret == 0) ioctl(dfd, BLKFLSBUF, 0);
    close(sfd);
    close(dfd);
    return ret;
}

// 递归删除文件/目录（等价 rm -rf）
static void rm_rf(const char *path) {
    struct stat st;
    if (lstat(path, &st) < 0) return;
    if (S_ISDIR(st.st_mode)) {
        DIR *d = opendir(path);
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL) {
                if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
                    continue;
                char sub[512];
                ssprintf(sub, sizeof(sub), "%s/%s", path, e->d_name);
                rm_rf(sub);
            }
            closedir(d);
        }
    }
    remove(path);
}

static const char *fstab_content =
"# Copyright (c) 2019-2020 The Linux Foundation. All rights reserved.\n"
"#\n"
"# Redistribution and use in source and binary forms, with or without\n"
"# modification, are permitted (subject to the limitations in the\n"
"# disclaimer below) provided that the following conditions are met:\n"
"#\n"
"#    * Redistributions of source code must retain the above copyright\n"
"#      notice, this list of conditions and the following disclaimer.\n"
"#\n"
"#    * Redistributions in binary form must reproduce the above\n"
"#      copyright notice, this list of conditions and the following\n"
"#      disclaimer in the documentation and/or other materials provided\n"
"#      with the distribution.\n"
"#\n"
"#    * Neither the name of The Linux Foundation nor the names of its\n"
"#      contributors may be used to endorse or promote products derived\n"
"#      from this software without specific prior written permission.\n"
"#\n"
"# NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE\n"
"# GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT\n"
"# HOLDERS AND CONTRIBUTORS \"AS IS\" AND ANY EXPRESS OR IMPLIED\n"
"# WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF\n"
"# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.\n"
"# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR\n"
"# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL\n"
"# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE\n"
"# GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS\n"
"# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER\n"
"# IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR\n"
"# OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN\n"
"# IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.\n"
"\n"
"# Android fstab file.\n"
"# The filesystem that contains the filesystem checker binary (typically /system) cannot\n"
"# specify MF_CHECK, and must come before any filesystems that do specify MF_CHECK\n"
"\n"
"#<src>                                                 <mnt_point>            <type>  <mnt_flags and options>                            <fs_mgr_flags>\n"
"/dev/block/by-name/oplusreserve2          /mnt/vendor/oplusreserve             ext4   nosuid,nodev,noatime,barrier=1                           wait,check,nofail,first_stage_mount\n"
"system                                                  /system                ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta_system,logical,first_stage_mount\n"
"system                                                  /system                erofs   ro                               wait,slotselect,avb=vbmeta_system,logical,first_stage_mount\n"
"system_ext                                              /system_ext            ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"system_ext                                              /system_ext            erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"product                                                 /product               ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"product                                                 /product               erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"vendor                                                  /vendor                ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta_vendor,logical,first_stage_mount\n"
"vendor                                                  /vendor                erofs   ro                               wait,slotselect,avb=vbmeta_vendor,logical,first_stage_mount\n"
"vendor_dlkm                                             /vendor_dlkm           ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"vendor_dlkm                                             /vendor_dlkm           erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"system_dlkm                                             /system_dlkm           ext4    ro,barrier=1,discard                                 wait,slotselect,avb=vbmeta,logical,first_stage_mount\n"
"system_dlkm                                             /system_dlkm           erofs   ro                               wait,slotselect,avb=vbmeta,logical,first_stage_mount\n"
"odm                                                     /odm                   ext4    ro,barrier=1,discard                                 wait,slotselect,logical,first_stage_mount\n"
"odm                                                     /odm                   erofs   ro                               wait,slotselect,logical,first_stage_mount\n"
"/dev/block/by-name/boot                                 /boot                  emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/init_boot                            /init_boot             emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/vendor_boot                          /vendor_boot           emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/dtbo                                 /dtbo                  emmc    defaults                                             slotselect,first_stage_mount\n"
"/dev/block/by-name/recovery                             /recovery              emmc    defaults                                             slotselect,first_stage_mount\n"
"# Mount my_xx to /my_xxx in first_stage_mount\n"
"my_product                 /my_product                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_product                 /my_product                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_engineering             /my_engineering                         ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_engineering             /my_engineering                         erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_company                 /my_company                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_company                 /my_company                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_carrier                 /my_carrier                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_carrier                 /my_carrier                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_region                  /my_region                              ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_region                  /my_region                              erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_heytap                  /my_heytap                              ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_heytap                  /my_heytap                              erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_stock                   /my_stock                               ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_stock                   /my_stock                               erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_preload                 /my_preload                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_preload                 /my_preload                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_bigball                 /my_bigball                             ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_bigball                 /my_bigball                             erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"my_manifest                /my_manifest                            ext4    ro,barrier=1       wait,slotselect,logical,first_stage_mount,nofail\n"
"my_manifest                /my_manifest                            erofs   ro                 wait,slotselect,logical,first_stage_mount,nofail\n"
"# Mount app,priv-app,lib64,lib of my_region/my_preload/my_heytap/my_engineering over /product/\n"
"overlay-overlay            /product/app                            overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/app:/my_preload/app:/my_product/app:/my_heytap/app:/my_stock/app:/my_engineering/app:/product/app                                    overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/priv-app                       overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/priv-app:/my_preload/priv-app:/my_product/priv-app:/my_heytap/priv-app:/my_stock/priv-app:/my_engineering/priv-app:/product/priv-app overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/lib64                          overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/lib64:/my_preload/lib64:/my_product/lib64:/my_heytap/lib64:/my_stock/lib64:/my_engineering/lib64:/product/lib64                      overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/lib                            overlay ro,seclabel,noatime,redirect_dir=nofollow,userxattr,lowerdir=/my_region/lib:/my_preload/lib:/my_product/lib:/my_heytap/lib:/my_stock/lib:/my_engineering/lib:/product/lib                                    overlayfs_remove_missing_lowerdir\n"
"# Mount etc/permissions,framework,media/audio/ui of my_product over /product/\n"
"overlay-overlay            /product/etc/permissions                overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/etc/permissions:/product/etc/permissions                                                                                    overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/framework                      overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/framework:/product/framework                                                                                                overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /product/media/audio/ui                 overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/ui:/product/media/audio/ui                                                                                      overlayfs_remove_missing_lowerdir\n"
"# Mount media/audio/ringtones/,media/audio/notifications/ of my_product over /system_ext\n"
"overlay-overlay            /system_ext/media/audio/ringtones       overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/ringtones:/system_ext/media/audio/ringtones                                                                     overlayfs_remove_missing_lowerdir\n"
"overlay-overlay            /system_ext/media/audio/notifications   overlay ro,seclabel,redirect_dir=nofollow,userxattr,lowerdir=/my_product/product_overlay/media/audio/notifications:/system_ext/media/audio/notifications                                                             overlayfs_remove_missing_lowerdir\n"
"/dev/block/by-name/metadata                             /metadata              f2fs    noatime,nosuid,nodev,discard                         wait,check,formattable,first_stage_mount\n"
"/dev/block/bootdevice/by-name/persist                   /mnt/vendor/persist    ext4    noatime,nosuid,nodev,barrier=1                       wait\n"
"/dev/block/bootdevice/by-name/userdata                  /data                  f2fs    noatime,nosuid,nodev,discard,reserve_root=32768,resgid=1065,fsync_mode=nobarrier,inlinecrypt   latemount,wait,check,formattable,fileencryption=aes-256-xts:aes-256-cts:v2+inlinecrypt_optimized+wrappedkey_v0,keydirectory=/metadata/vold/metadata_encryption,metadata_encryption=aes-256-xts:wrappedkey_v0,quota,reservedsize=128M,sysfs_path=/sys/devices/platform/soc/1d84000.ufshc,checkpoint=fs\n"
"/dev/block/by-name/misc                                 /misc                  emmc    defaults                                             defaults\n"
"/devices/platform/soc/8804000.sdhci/mmc_host*           /storage/sdcard1       vfat    nosuid,nodev                                         wait,voldmanaged=sdcard1:auto,encryptable=footer\n"
"/devices/platform/soc/*.ssusb/*.dwc3/xhci-hcd.*.auto*   /storage/usbotg        vfat    nosuid,nodev,readwrite                              wait,voldmanaged=usbotg:auto\n"
"/dev/block/bootdevice/by-name/modem                     /vendor/firmware_mnt   vfat    ro,uid=1000,gid=1000,dmask=227,fmask=337,context=u:object_r:firmware_file:s0 wait,slotselect\n"
"/dev/block/bootdevice/by-name/dsp                       /vendor/dsp            ext4    ro,nosuid,nodev,barrier=1                            wait,slotselect\n"
"/dev/block/bootdevice/by-name/vm-bootsys                /vendor/vm-system     ext4    ro,nosuid,nodev,barrier=1                            wait,slotselect\n"
"/dev/block/bootdevice/by-name/vm-persist                /mnt/vendor/vm-persist     ext4    noatime,nosuid,nodev,barrier=1                  wait\n"
"/dev/block/bootdevice/by-name/bluetooth                 /vendor/bt_firmware    vfat    ro,shortname=lower,uid=1002,gid=3002,dmask=227,fmask=337,context=u:object_r:bt_firmware_file:s0 wait,slotselect\n"
"/dev/block/bootdevice/by-name/qmcs                      /mnt/vendor/qmcs       vfat    noatime,nosuid,nodev,context=u:object_r:vendor_qmcs_file:s0   wait,check,formattable\n"
"/dev/block/bootdevice/by-name/spunvm                    /mnt/vendor/spunvm     vfat    noatime,nosuid,nodev,context=u:object_r:vendor_spunvm_file:s0   wait,check,formattable\n"
"/dev/block/bootdevice/by-name/soccp                     /vendor_soccp_firmware vfat    ro,shortname=lower,uid=0,gid=1000,dmask=227,fmask=337,context=u:object_r:vendor_soccp_file:s0 wait,slotselect\n";

static bool file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

static int filter_file(const char *filepath, const char *key) {
    FILE *src = fopen(filepath, "r");
    if (!src) return 0;

    char tmp_path[512];
    ssprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);
    FILE *dst = fopen(tmp_path, "w");
    if (!dst) {
        fclose(src);
        return 0;
    }

    char line[BUF_SIZE];
    while (fgets(line, sizeof(line), src)) {
        if (strstr(line, key) == NULL) {
            fputs(line, dst);
        }
    }

    fclose(src);
    fclose(dst);
    rename(tmp_path, filepath);
    return 1;
}

static void write_fstab_file() {
    FILE *fp = fopen("fstab.qcom", "w");
    if (fp) {
        fputs(fstab_content, fp);
        fclose(fp);
    }
}

static void update_header_cmdline(const char *header_path, const char *param) {
    FILE *fp = fopen(header_path, "r");
    if (!fp) return;

    char buffer[BUF_SIZE * 2] = {0};
    char line[BUF_SIZE];
    int found = 0;

    while (fgets(line, sizeof(line), fp)) {
        if (strstr(line, param) != NULL) {
            found = 1;
        }
        if (strncmp(line, "cmdline=", 8) == 0) {
            line[strcspn(line, "\r\n")] = 0;
            if (strstr(line, param) == NULL) {
                ssprintf(line + strlen(line), sizeof(line) - strlen(line), " %s\n", param);
            } else {
                strcat(line, "\n");
            }
        }
        strcat(buffer, line);
    }
    fclose(fp);

    if (!found) {
        fp = fopen(header_path, "w");
        if (fp) {
            fputs(buffer, fp);
            fclose(fp);
        }
    }
}

static int parse_slot(const char *arg, char *out_slot) {
    if (!arg) return 0;
    if (strcasecmp(arg, "a") == 0 || strcasecmp(arg, "_a") == 0) {
        strcpy(out_slot, "_a");
        return 1;
    }
    if (strcasecmp(arg, "b") == 0 || strcasecmp(arg, "_b") == 0) {
        strcpy(out_slot, "_b");
        return 1;
    }
    return 0;
}

int patch_vendor_boot(int argc, char *argv[]) {
    char slot[16] = {0};
    int super_mode = 0;

    if (argc > 0 && !parse_slot(argv[0], slot)) {
        printf("[!] 无效的参数: %s\n", argv[0]);
    }
    if (argc > 1 && strcmp(argv[1], "super") == 0) {
        super_mode = 1;
    }

    if (strlen(slot) == 0) {
        printf("[!] 未指定有效槽位 (a/b)\n");
        return 1;
    }

    printf("[+] 目标槽位: %s\n", slot);
    printf("[+] 请等待...\n");

    char blk_path[128];
    ssprintf(blk_path, sizeof(blk_path), "/dev/block/by-name/vendor_boot%s", slot);

    if (copy_file(blk_path, "vendor_boot.img") != 0 || !file_exists("vendor_boot.img")) {
        printf("[!] 提取 vendor_boot%s 失败！\n", slot);
        return 1;
    }

    // 解包 vendor_boot（-h 模式：dump header，供 repack 解析）
    unpack("vendor_boot.img", false, true);

    // vendor_boot header v4：默认 vendor ramdisk 解包到 vendor_ramdisk/ramdisk.cpio
    // 更早版本：ramdisk.cpio
    const char *target_cpio = "vendor_ramdisk/ramdisk.cpio";
    if (!file_exists(target_cpio)) {
        target_cpio = "ramdisk.cpio";
    }
    if (!file_exists(target_cpio)) {
        printf("[!] 解包失败或未找到 ramdisk\n");
        return 1;
    }

    // 提取 modules.load.recovery（兼容两种路径）
    const char *entry = "lib/modules/modules.load.recovery";
    char cpio_cmd[512];
    ssprintf(cpio_cmd, sizeof(cpio_cmd), "extract %s tmp_modules.load.recovery", entry);
    {
        const char *cpio_argv[] = {target_cpio, cpio_cmd};
        rust::cpio_commands(2, cpio_argv);
    }
    if (!file_exists("tmp_modules.load.recovery")) {
        entry = "modules.load.recovery";
        ssprintf(cpio_cmd, sizeof(cpio_cmd), "extract %s tmp_modules.load.recovery", entry);
        const char *cpio_argv[] = {target_cpio, cpio_cmd};
        rust::cpio_commands(2, cpio_argv);
    }

    if (file_exists("tmp_modules.load.recovery")) {
        filter_file("tmp_modules.load.recovery", "oplus_secure_guard_new");
        ssprintf(cpio_cmd, sizeof(cpio_cmd), "add 0644 %s tmp_modules.load.recovery", entry);
        const char *cpio_argv[] = {target_cpio, cpio_cmd};
        rust::cpio_commands(2, cpio_argv);
        unlink("tmp_modules.load.recovery");
    } else {
        printf("[!] 未找到 modules.load.recovery 文件\n");
        return 1;
    }

    if (super_mode) {
        write_fstab_file();
        const char *cpio_argv[] = {target_cpio, "add 0644 first_stage_ramdisk/fstab.qcom fstab.qcom"};
        rust::cpio_commands(2, cpio_argv);
    }

    // 修改 header cmdline
    if (file_exists("header")) {
        update_header_cmdline("header", "module_blacklist=oplus_secure_guard_new");
    }

    // 重新打包
    repack("vendor_boot.img", "new_vendor_boot.img");

    if (file_exists("new_vendor_boot.img")) {
        if (write_to_block("new_vendor_boot.img", blk_path) != 0) {
            printf("[!] 写入 vendor_boot%s 失败！\n", slot);
            return 1;
        }

        printf("[+] 【槽位 %s 处理完成】\n", slot);

        // 清理临时文件
        rm_rf("vendor_ramdisk");
        unlink("ramdisk.cpio");
        unlink("bootconfig");
        unlink("dtb");
        unlink("header");
        unlink("kernel");
        unlink("kernel_dtb");
        unlink("second");
        unlink("extra");
        unlink("recovery_dtbo");
        unlink("vendor_boot.img");
        unlink("new_vendor_boot.img");
        unlink("fstab.qcom");
    } else {
        printf("[!] 打包 new_vendor_boot.img 失败\n");
        return 1;
    }

    return 0;
}
