#pragma once
#include <string>

#include "core/hal.h"

// The knob on the network: the pairing key Knob Hub gives it over USB, Wi-Fi (joining the saved network, and pairing
// mode: its own network with a setup page), and the link to Knob Hub over Wi-Fi (a TCP server, authenticated with the
// key and sealed; see core/link.h). The hal_wifi_* and hal_identity calls are here too.
namespace net {

// In setup(): reads the key and the saved network, and joins it. tx_power: the radio's transmit power, in the Wi-Fi
// driver's quarter dBm (wifi_power_t, e.g. WIFI_POWER_15dBm), for boards that need it lower; 0 leaves the driver's.
void start(int tx_power = 0);
// In loop(): Wi-Fi, the setup page and the link. Calls knob_os_wifi_changed() when the Wi-Fi state changes.
void poll();

// A {"t":"pair"} line that came over USB: keeps its key.
void pair(const std::string& line);

// Knob Hub over Wi-Fi: connected and authenticated. generation() counts the connections, so a new one (Knob Hub
// started again) is noticed even when there was no gap.
bool linked();
unsigned generation();
// The next line Knob Hub sent, opened; false when there's none.
bool next_line(std::string& line);
void send(const char* line);
// Lets go of Knob Hub over Wi-Fi (USB took over, or it went quiet).
void drop();

}  // namespace net
