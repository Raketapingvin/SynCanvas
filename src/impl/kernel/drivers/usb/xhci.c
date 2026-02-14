#include <drivers/usb/xhci.h>
#include <drivers/pci.h>

// Class 0x0C = Serial Bus Controller
// Subclass 0x03 = USB Controller
// Programming Interface 0x30 = xHCI (USB 3.0)

bool xhci_init() {
    // We search for a PCI device with Class 0x0C, Subclass 0x03.
    // NOTE: This helper function finds the *first* match.
    // In a real driver we would iterate all.
    struct PciDevice dev = pci_get_device_by_class(0x0C, 0x03);
    
    if (dev.vendor_id != 0xFFFF) {
        // We found a USB controller. Let's check the Programming Interface (ProgIF).
        // The ProgIF is in the Revision ID register (offset 0x08), roughly.
        // Reading Offset 0x08 returns: [Class Code | Subclass | Prog IF | Revision]
        uint32_t val = pci_read(dev.bus, dev.device, dev.function, 0x08);
        uint8_t prog_if = (val >> 8) & 0xFF;
        
        if (prog_if == 0x30) {
            return true; // It is xHCI!
        }
    }
    return false;
}
