#include <drivers/storage/ahci.h>
#include <drivers/pci.h>
#include <drivers/display/text.h>

// Class 0x01 = Mass Storage
// Subclass 0x06 = SATA (AHCI)
#define CLASS_STORAGE 0x01
#define SUBCLASS_SATA 0x06

bool ahci_init() {
    struct PciDevice dev = pci_get_device_by_class(CLASS_STORAGE, SUBCLASS_SATA);
    
    if (dev.vendor_id != 0xFFFF) {
        // Found a SATA Controller!
        return true;
    }
    return false;
}
