#include <drivers/storage/nvme.h>
#include <drivers/pci.h>
#include <drivers/display/text.h>

// Class 0x01 = Mass Storage
// Subclass 0x08 = Non-Volatile Memory (NVMe)
#define CLASS_STORAGE 0x01
#define SUBCLASS_NVME 0x08

bool nvme_init() {
    // NVMe is standard PCI class 0x01, subclass 0x08.
    // Most M.2 drives appear as this.
    struct PciDevice dev = pci_get_device_by_class(CLASS_STORAGE, SUBCLASS_NVME);
    
    if (dev.vendor_id != 0xFFFF) {
        return true;
    }
    return false;
}
