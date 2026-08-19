#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#define BUF_SIZE 4096

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

void get_script_dir(char *out_path, size_t size) {
    char exe_path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len != -1) {
        exe_path[len] = '\0';
        char *last_slash = strrchr(exe_path, '/');
        if (last_slash) {
            *last_slash = '\0';
            strncpy(out_path, exe_path, size);
            return;
        }
    }
    getcwd(out_path, size);
}

int file_exists(const char *filename) {
    struct stat buffer;
    return (stat(filename, &buffer) == 0);
}

int filter_file(const char *filepath, const char *key) {
    FILE *src = fopen(filepath, "r");
    if (!src) return 0;

    char tmp_path[512];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", filepath);
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

void write_fstab_file() {
    FILE *fp = fopen("fstab.qcom", "w");
    if (fp) {
        fputs(fstab_content, fp);
        fclose(fp);
    }
}

void update_header_cmdline(const char *header_path, const char *param) {
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
                snprintf(line + strlen(line), sizeof(line) - strlen(line), " %s\n", param);
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

int parse_slot(const char *arg, char *out_slot) {
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

int main(int argc, char *argv[]) {
    char slot[16] = {0};
    int super_mode = 0;

    if (argc > 1) {
        if (!parse_slot(argv[1], slot)) {
            printf("[!] 无效的参数: %s\n", argv[1]);
        }
    }
    if (argc > 2 && strcmp(argv[2], "super") == 0) {
        super_mode = 1;
    }

    if (strlen(slot) == 0) {
        char input_buf[32] = {0};
        while (1) {
            printf("[?] 请选择要修补的槽位 (a/b): ");
            if (fgets(input_buf, sizeof(input_buf), stdin) != NULL) {
                char *p = input_buf;
                while (isspace(*p)) p++;
                p[strcspn(p, "\r\n")] = 0;

                if (parse_slot(p, slot)) {
                    break;
                }
            }
            printf("[!] 输入无效，请输入 'a' 或 'b'\n");
        }
    }

    printf("[+] 目标槽位: %s\n", slot);
    printf("[+] 请等待...\n");

    char script_dir[PATH_MAX];
    get_script_dir(script_dir, sizeof(script_dir));

    char mboot_bin[PATH_MAX];
    snprintf(mboot_bin, sizeof(mboot_bin), "%s/mboot", script_dir);
    chmod(mboot_bin, 0755);

    char cmd[BUF_SIZE];
    snprintf(cmd, sizeof(cmd), "dd if=/dev/block/by-name/vendor_boot%s of=vendor_boot.img bs=4M >/dev/null 2>&1", slot);
    system(cmd);

    if (!file_exists("vendor_boot.img")) {
        printf("[!] 提取 vendor_boot%s 失败！\n", slot);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "\"%s\" unpack -h vendor_boot.img >/dev/null 2>&1", mboot_bin);
    system(cmd);

    const char *target_cpio = "vendor_ramdisk/ramdisk.cpio";
    if (!file_exists(target_cpio)) {
        printf("[!] 解包失败或未找到 %s\n", target_cpio);
        return 1;
    }

    snprintf(cmd, sizeof(cmd), "\"%s\" cpio \"%s\" \"extract lib/modules/modules.load.recovery tmp_modules.load.recovery\" >/dev/null 2>&1", mboot_bin, target_cpio);
    system(cmd);

    if (!file_exists("tmp_modules.load.recovery")) {
        snprintf(cmd, sizeof(cmd), "\"%s\" cpio \"%s\" \"extract modules.load.recovery tmp_modules.load.recovery\" >/dev/null 2>&1", mboot_bin, target_cpio);
        system(cmd);
    }

    if (file_exists("tmp_modules.load.recovery")) {
        filter_file("tmp_modules.load.recovery", "oplus_secure_guard_new");

        snprintf(cmd, sizeof(cmd), "\"%s\" cpio \"%s\" \"add 0777 lib/modules/modules.load.recovery tmp_modules.load.recovery\" >/dev/null 2>&1", mboot_bin, target_cpio);
        system(cmd);
        unlink("tmp_modules.load.recovery");
    } else {
        printf("[!] 未找到 modules.load.recovery 文件\n");
        return 1;
    }

    if (super_mode) {
        write_fstab_file();
        snprintf(cmd, sizeof(cmd), "\"%s\" cpio \"%s\" \"add 0777 first_stage_ramdisk/fstab.qcom fstab.qcom\" >/dev/null 2>&1", mboot_bin, target_cpio);
        system(cmd);
    }

    if (file_exists("header")) {
        update_header_cmdline("header", "module_blacklist=oplus_secure_guard_new");
    }

    snprintf(cmd, sizeof(cmd), "\"%s\" repack vendor_boot.img new_vendor_boot.img >/dev/null 2>&1", mboot_bin);
    system(cmd);

    if (file_exists("new_vendor_boot.img")) {
        snprintf(cmd, sizeof(cmd), "dd if=new_vendor_boot.img of=/dev/block/by-name/vendor_boot%s bs=4M conv=fsync >/dev/null 2>&1", slot);
        system(cmd);

        printf("[+] 【槽位 %s 处理完成】\n", slot);

        system("rm -rf vendor_ramdisk bootconfig dtb header kernel vendor_boot.img new_vendor_boot.img fstab.qcom 2>/dev/null");
    } else {
        printf("[!] 打包 new_vendor_boot.img 失败\n");
        return 1;
    }

    return 0;
}
