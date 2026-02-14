#include <drivers/audio/hda.h>
#include <drivers/pci.h>

// Class 0x04 = Multimedia Controller
// Subclass 0x03 = Audio Device (HDA)
// (Sometimes 0x0401 is subclass Audio)
// Intel HDA often ID: 8086:2668 or similar.
// Generic Class seach is safer.

bool hda_init() {
    // Search for Multimedia Controller (0x04) / Audio (0x03)
    struct PciDevice dev = pci_get_device_by_class(0x04, 0x03);
    
    if (dev.vendor_id != 0xFFFF) {
        return true;
    }
    
    // Sometimes HDA is under 0x04 / 0x01 (Multimedia Audio)
    struct PciDevice dev2 = pci_get_device_by_class(0x04, 0x01);
    if (dev2.vendor_id != 0xFFFF) {
        return true;
    }

    return false;
}
