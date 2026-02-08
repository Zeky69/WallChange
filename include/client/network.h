#ifndef NETWORK_H
#define NETWORK_H

void set_local_mode(int enabled);
void set_admin_token(const char *token);
void check_and_update_version(const char *client_version);
void init_network();
void cleanup_network();
void connect_ws();
void network_poll(int timeout_ms);
void send_client_info();
int send_command(const char *arg1, const char *arg2);
int send_update_command(const char *target_user);
int send_list_command();
int send_showdesktop_command(const char *target_user);
int send_key_command(const char *target_user, const char *combo);
int send_reverse_command(const char *target_user);
int send_marquee_command(const char *target_user, const char *url);
int send_particles_command(const char *target_user, const char *url);
int send_cover_command(const char *target_user, const char *url);
int send_clones_command(const char *target_user);
int send_drunk_command(const char *target_user);
int send_uninstall_command(const char *target_user);
int send_reinstall_command(const char *target_user);
int watch_logs(const char *target_user);
int send_login_command(const char *user, const char *pass);
int send_faketerminal_command(const char *target_user);
int send_confetti_command(const char *target_user, const char *url);
int send_spotlight_command(const char *target_user);
int send_textscreen_command(const char *target_user, const char *text);
int send_wavescreen_command(const char *target_user);
int send_dvdbounce_command(const char *target_user, const char *url);
int send_fireworks_command(const char *target_user);
int send_lock_command(const char *target_user);
int send_blackout_command(const char *target_user);

#endif
