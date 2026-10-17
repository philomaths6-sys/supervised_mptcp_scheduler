#ifndef LINK_PROFILE_H
#define LINK_PROFILE_H

#include <string>
#include <vector>

namespace ns3 {

// Represents an individual network path/subflow characteristics
struct PathConfig
{
  std::string name;          // e.g. "Ethernet", "WiFi", "Cellular"
  std::string bandwidth;     // e.g. "100Mbps", "40Mbps", "15Mbps"
  std::string delay;         // e.g. "5ms", "25ms", "80ms"
  double lossRate;           // e.g. 0.0, 0.005 (0.5%), 0.01 (1%)
  uint32_t queueSizePackets; // e.g. 100, 60, 40

  PathConfig (std::string n, std::string bw, std::string d, double loss = 0.0, uint32_t q = 100)
    : name (n), bandwidth (bw), delay (d), lossRate (loss), queueSizePackets (q) {}
};

// Represents a real-world multi-interface device profile
enum DeviceProfileType
{
  PROFILE_WIFI_CELLULAR,     // Smartphone: Wi-Fi + 4G/5G Cellular
  PROFILE_ETHERNET_WIFI,     // Workstation/Desktop: Ethernet + Wi-Fi
  PROFILE_TRIPLE_PATH,       // Multihomed Gateway: Ethernet + Wi-Fi + Cellular
  PROFILE_CUSTOM             // User defined parameters
};

class LinkProfileManager
{
public:
  static std::vector<PathConfig> GetProfile (DeviceProfileType type)
  {
    std::vector<PathConfig> paths;
    switch (type)
      {
      case PROFILE_WIFI_CELLULAR:
        // Path 0: Wi-Fi (802.11ac)
        paths.push_back (PathConfig ("WiFi", "50Mbps", "15ms", 0.002, 60));
        // Path 1: Cellular (4G LTE / 5G)
        paths.push_back (PathConfig ("Cellular", "20Mbps", "65ms", 0.008, 40));
        break;

      case PROFILE_ETHERNET_WIFI:
        // Path 0: Fast Ethernet
        paths.push_back (PathConfig ("Ethernet", "100Mbps", "5ms", 0.0, 100));
        // Path 1: Wi-Fi
        paths.push_back (PathConfig ("WiFi", "35Mbps", "25ms", 0.003, 50));
        break;

      case PROFILE_TRIPLE_PATH:
        // Path 0: Ethernet
        paths.push_back (PathConfig ("Ethernet", "100Mbps", "5ms", 0.0, 100));
        // Path 1: Wi-Fi
        paths.push_back (PathConfig ("WiFi", "40Mbps", "25ms", 0.005, 60));
        // Path 2: Cellular
        paths.push_back (PathConfig ("Cellular", "15Mbps", "80ms", 0.01, 40));
        break;

      case PROFILE_CUSTOM:
      default:
        // Default dual-path
        paths.push_back (PathConfig ("Path0", "20Mbps", "10ms", 0.0, 100));
        paths.push_back (PathConfig ("Path1", "10Mbps", "50ms", 0.0, 100));
        break;
      }
    return paths;
  }
};

} // namespace ns3

#endif // LINK_PROFILE_H
