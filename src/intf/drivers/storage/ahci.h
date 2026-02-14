#pragma once
#include <stdint.h>
#include <stdbool.h>

// Detects AHCI (SATA) Controllers
// Class 0x01, Subclass 0x06
bool ahci_init();
