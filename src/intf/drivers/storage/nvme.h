#pragma once
#include <stdint.h>
#include <stdbool.h>

// Detects NVMe (M.2/PCIe) Controllers
// Class 0x01, Subclass 0x08
bool nvme_init();
