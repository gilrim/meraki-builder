#ifndef CONFIGD_H
#define CONFIGD_H

#include "hardware.h"
#include <libpd690xx.h>
#include <stdbool.h>

extern bool dry_run;
extern const char *config_file;
extern char meraki_mac[18];
extern struct pd690xx_cfg pd690xx;
extern struct hardware_info hardware;

#endif
