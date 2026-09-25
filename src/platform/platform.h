/* cosmo-bde — platform layer interface (see platform.c) */
#ifndef COSMO_BDE_PLATFORM_H_
#define COSMO_BDE_PLATFORM_H_

#include <stdint.h>

int plat_file_exists(const char *path);
int plat_mkdir(const char *path);
int plat_is_dir(const char *path);

int64_t plat_time_ms(void);
void plat_sleep_ms(int ms);

const char *plat_os_name(void);   /* "Linux", "macOS", "Windows", ... */
const char *plat_arch_name(void); /* "x86_64", "arm64", ... */
int plat_is_ape(void);            /* 1 when built by cosmocc */
void plat_print_info(void);

#endif /* COSMO_BDE_PLATFORM_H_ */
