#ifndef NETWORK_H
#define NETWORK_H

void set_local_mode(int enabled);
void init_network();
void cleanup_network();
void connect_ws();
void network_poll(int timeout_ms);
int send_command(const char *arg1, const char *arg2);
int send_update_command(const char *target_user);
int send_list_command();
int send_showdesktop_command(const char *target_user);
int send_key_command(const char *target_user, const char *combo);

#endif
