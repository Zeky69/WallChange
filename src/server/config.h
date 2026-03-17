#ifndef CONFIG_H
#define CONFIG_H

const char *wc_getenv_default(const char *key, const char *default_value);
void wc_apply_default_environment(void);

#endif