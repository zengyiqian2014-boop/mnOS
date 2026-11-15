/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MNOS_USB_PLATFORM_H
#define MNOS_USB_PLATFORM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void usb_platform_init(void);
uint64_t usb_millis(void);
void usb_delay(unsigned);
void usb_dma_sync(void *, size_t);
void *usb_map_mmio(uint64_t, size_t);
bool usb_pci_window(unsigned *, unsigned *);
uint32_t usb_pci_read(unsigned, unsigned, unsigned, unsigned);
void usb_pci_write(unsigned, unsigned, unsigned, unsigned, uint32_t);
void *memory_map_device(uint64_t, size_t);
#endif
