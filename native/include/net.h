/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_NET_H
#define MNOS_NET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* A dedicated Ethernet device for the software guest; no host sockets or
 * privileged host network commands are reachable from guest packets. */
bool net_start(uint8_t mac[6]);
void net_stop(void);
int net_receive(void *,size_t);
int net_transmit(const void *,size_t);
#endif
