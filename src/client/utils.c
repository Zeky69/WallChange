#include "client/utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <stdio.h>
#include <sys/sysinfo.h>

char *get_username() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return strdup(pw->pw_name);
    }
    return strdup(getenv("USER"));
}

char *get_os_info() {
    static char os_info[128] = {0};
    FILE *fp = fopen("/etc/os-release", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
                // Extraire la valeur entre guillemets
                char *start = strchr(line, '"');
                if (start) {
                    start++;
                    char *end = strchr(start, '"');
                    if (end) {
                        *end = '\0';
                        strncpy(os_info, start, sizeof(os_info) - 1);
                    }
                }
                break;
            }
        }
        fclose(fp);
    }
    if (os_info[0] == '\0') {
        strcpy(os_info, "Linux");
    }
    return os_info;
}

char *get_uptime() {
    static char uptime_str[64] = {0};
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        long uptime = si.uptime;
        int days = uptime / 86400;
        int hours = (uptime % 86400) / 3600;
        int mins = (uptime % 3600) / 60;
        
        if (days > 0) {
            snprintf(uptime_str, sizeof(uptime_str), "%dj %dh %dm", days, hours, mins);
        } else if (hours > 0) {
            snprintf(uptime_str, sizeof(uptime_str), "%dh %dm", hours, mins);
        } else {
            snprintf(uptime_str, sizeof(uptime_str), "%dm", mins);
        }
    } else {
        strcpy(uptime_str, "N/A");
    }
    return uptime_str;
}

char *get_cpu_load() {
    static char cpu_str[32] = {0};
    double loadavg[3];
    if (getloadavg(loadavg, 3) != -1) {
        snprintf(cpu_str, sizeof(cpu_str), "%.2f, %.2f, %.2f", 
                 loadavg[0], loadavg[1], loadavg[2]);
    } else {
        strcpy(cpu_str, "N/A");
    }
    return cpu_str;
}

char *get_ram_usage() {
    static char ram_str[32] = {0};
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        unsigned long total_mb = si.totalram / (1024 * 1024);
        unsigned long free_mb = si.freeram / (1024 * 1024);
        unsigned long used_mb = total_mb - free_mb;
        int percent = (int)((used_mb * 100) / total_mb);
        snprintf(ram_str, sizeof(ram_str), "%lu/%luMB (%d%%)", 
                 used_mb, total_mb, percent);
    } else {
        strcpy(ram_str, "N/A");
    }
    return ram_str;
}

void execute_reverse_screen() {
    int ret;
    ret = system("xrandr -o inverted");
    (void)ret;
    sleep(3);
    ret = system("xrandr -o normal");
    (void)ret;
}

