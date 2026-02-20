#pragma once
#include <stdint.h>

void rtl8139_init();
int rtl8139_is_detected();
void rtl8139_get_mac(uint8_t* out);
int rtl8139_send_packet(void* data, uint32_t len);
int rtl8139_arp_ping(uint8_t ip0, uint8_t ip1, uint8_t ip2, uint8_t ip3);
