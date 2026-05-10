#ifndef AUTH_H
#define AUTH_H

#include "common.h"

typedef struct {
    char username[32];
    char password[32];
    u8 authenticated;
} user_session_t;

void auth_login(user_session_t *session);
u8 auth_verify_password(const char *username, const char *password);
void auth_show_login_prompt(void);

#endif /* AUTH_H */
