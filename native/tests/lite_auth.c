/* SPDX-License-Identifier: GPL-3.0-or-later */
#define main full_factor_test_main
#include "factor.c"
#undef main
int main(void) {
    auth_init(&io);auth_tick(mono);
    input("alice");password("Ordinary-test-491");password("Root-admin-test-785");
    assert(auth_logged_in()&&!auth_prompting()&&!auth_factors_available());
    command("2fa enable");assert(strstr(output,"Lite")&&!auth_prompting()&&!*auth_setup_secret());
    command("lock");login("Ordinary-test-491");assert(auth_logged_in());
    /* Booting Lite must never bypass factors enabled by a Full installation. */
    saved.users[1].factor.enabled=1;auth_init(&io);auth_tick(mono);
    login("Ordinary-test-491");assert(!auth_logged_in()&&!strstr(auth_prompt(),"Authenticator"));
    assert(strstr(auth_status(),"Full edition"));
    puts("PASS Native Lite: password-only setup/login, no factor enrollment, enrolled Full accounts cannot bypass second-factor authentication");
    return 0;
}
