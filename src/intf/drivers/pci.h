#pragma once
#include <stdint.h>

struct PciDevice {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t bar0; // Base Address Register 0
    uint8_t irq_line;
};

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t register_offset);
void pci_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t register_offset, uint32_t value);
struct PciDevice pci_get_device(uint16_t vendor_id, uint16_t device_id);
struct PciDevice pci_get_device_by_class(uint8_t class_code, uint8_t subclass_code);
void pci_enable_bus_mastering(struct PciDevice dev);

