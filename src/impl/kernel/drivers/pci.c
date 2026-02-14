#include "drivers/pci.h"
#include "util/io.h"

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA 0xCFC

uint32_t pci_read(uint8_t bus, uint8_t device, uint8_t func, uint8_t register_offset) {
    uint32_t id = 
        (1 << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)func << 8) |
        (register_offset & 0xFC);
        
    outl(PCI_CONFIG_ADDRESS, id);
    return inl(PCI_CONFIG_DATA);
}

void pci_write(uint8_t bus, uint8_t device, uint8_t func, uint8_t register_offset, uint32_t value) {
    uint32_t id = 
        (1 << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)func << 8) |
        (register_offset & 0xFC);
        
    outl(PCI_CONFIG_ADDRESS, id);
    outl(PCI_CONFIG_DATA, value);
}

struct PciDevice pci_get_device(uint16_t vendor_id, uint16_t device_id) {
    struct PciDevice dev = {0};
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id_reg = pci_read(bus, device, func, 0x00);
                if ((id_reg & 0xFFFF) != 0xFFFF) { // Device exists
                    uint16_t v_id = id_reg & 0xFFFF;
                    uint16_t d_id = (id_reg >> 16) & 0xFFFF;
                    
                    if (v_id == vendor_id && d_id == device_id) {
                        dev.bus = bus;
                        dev.device = device;
                        dev.function = func;
                        dev.vendor_id = v_id;
                        dev.device_id = d_id;
                        
                        uint32_t bar0 = pci_read(bus, device, func, 0x10);
                        dev.bar0 = bar0;
                        
                        uint32_t intr_line = pci_read(bus, device, func, 0x3C);
                        dev.irq_line = intr_line & 0xFF;
                        
                        return dev;
                    }
                }
            }
        }
    }
    return dev;
}
struct PciDevice pci_get_device_by_class(uint8_t class_code, uint8_t subclass_code) {
    struct PciDevice dev = {0};
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t id_reg = pci_read(bus, device, func, 0x00);
                if ((id_reg & 0xFFFF) != 0xFFFF) { // Device exists
                    
                    uint32_t class_reg = pci_read(bus, device, func, 0x08);
                    uint8_t cls = (class_reg >> 24) & 0xFF;
                    uint8_t sub = (class_reg >> 16) & 0xFF; // Subclass
                    
                    if (cls == class_code && sub == subclass_code) {
                        dev.bus = bus;
                        dev.device = device;
                        dev.function = func;
                        dev.vendor_id = id_reg & 0xFFFF;
                        dev.device_id = (id_reg >> 16) & 0xFFFF;
                        
                        uint32_t bar0 = pci_read(bus, device, func, 0x10);
                        dev.bar0 = bar0;
                        
                        return dev;
                    }
                }
            }
        }
    }
    return dev;
}
void pci_enable_bus_mastering(struct PciDevice dev) {
    uint32_t command_reg = pci_read(dev.bus, dev.device, dev.function, 0x04);
    command_reg |= (1 << 2); // Set Bit 2 (Bus Mastering)
    pci_write(dev.bus, dev.device, dev.function, 0x04, command_reg);
}
