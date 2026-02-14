#include <drivers/audio/ac97.h>
#include <drivers/pci.h>

// Class 0x04 = Multimedia Controller
// Subclass 0x01 = Multimedia Audio (Often AC97)
bool ac97_init() {
    struct PciDevice dev = pci_get_device_by_class(0x04, 0x01);
    
    if (dev.vendor_id != 0xFFFF) {
        return true;
    }
    return false;
}
