#include "client/utils.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>

char *get_username() {
    struct passwd *pw = getpwuid(getuid());
    if (pw) {
        return strdup(pw->pw_name);
    }
    return strdup(getenv("USER"));
}
