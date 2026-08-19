#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <limits.h>
#include <ctype.h>

#define BUF_SIZE 4096

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

    if (argc > 1) {
        if (!parse_slot(argv[1], slot)) {
            printf("[!] 无效的参数: %s\n", argv[1]);
        }
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