#ifndef UTILS_H
#define UTILS_H

char *get_username();
char *get_hostname();
char *get_os_info();
char *get_uptime();
char *get_cpu_load();
char *get_ram_usage();
void execute_reverse_screen();
void execute_marquee(const char *url);
void execute_particles(const char *url);
void execute_clones(void);
void execute_pong(const char *opponent_user, int is_left_side);

#endif

