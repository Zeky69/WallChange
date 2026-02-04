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
void execute_drunk(void);
void execute_faketerminal(void);
void execute_confetti(const char *url);
void execute_spotlight(void);
void execute_textscreen(const char *text);
void execute_wavescreen(void);
void execute_dvdbounce(const char *url);
void execute_fireworks(void);
void execute_lock(void);
void execute_nyancat(void);
void execute_fly(void);
void execute_invert(void);

#endif

