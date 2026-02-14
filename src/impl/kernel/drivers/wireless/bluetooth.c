#include <drivers/wireless/bluetooth.h>
#include <drivers/pci.h>

// Class 0x0D = Wireless Controller
// Subclass 0x11 = Bluetooth
bool bluetooth_init() {
    struct PciDevice dev = pci_get_device_by_class(0x0D, 0x11);
    
    if (dev.vendor_id != 0xFFFF) {
        return true;
    }
    return false;
}
