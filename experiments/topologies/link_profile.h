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
        // Path 0: Wi-Fi (15 Mbps, 15ms, 0.1% loss, 300 pkts buffer)
        paths.push_back (PathConfig ("WiFi", "15Mbps", "15ms", 0.001, 300));
        // Path 1: Cellular 4G LTE (5 Mbps, 50ms, 0.3% loss, 200 pkts buffer)
        paths.push_back (PathConfig ("Cellular", "5Mbps", "50ms", 0.003, 200));
        break;

      case PROFILE_ETHERNET_WIFI:
        // Path 0: Ethernet (25 Mbps, 5ms, 0 loss, 400 pkts buffer)
        paths.push_back (PathConfig ("Ethernet", "25Mbps", "5ms", 0.0, 400));
        // Path 1: Wi-Fi (10 Mbps, 25ms, 0.2% loss, 250 pkts buffer)
        paths.push_back (PathConfig ("WiFi", "10Mbps", "25ms", 0.002, 250));
        break;

      case PROFILE_TRIPLE_PATH:
        // Path 0: Ethernet (20 Mbps, 5ms, 0 loss, 350 pkts buffer)
        paths.push_back (PathConfig ("Ethernet", "20Mbps", "5ms", 0.0, 350));
        // Path 1: Wi-Fi (10 Mbps, 25ms, 0.2% loss, 250 pkts buffer)
        paths.push_back (PathConfig ("WiFi", "10Mbps", "25ms", 0.002, 250));
        // Path 2: Cellular (5 Mbps, 60ms, 0.5% loss, 200 pkts buffer)
        paths.push_back (PathConfig ("Cellular", "5Mbps", "60ms", 0.005, 200));
        break;

      case PROFILE_CUSTOM:
      default:
        // Canonical Research Ratio: Ethernet 5Mbps + WiFi 1Mbps
        paths.push_back (PathConfig ("Path0", "5Mbps", "10ms", 0.0, 250));
        paths.push_back (PathConfig ("Path1", "1Mbps", "30ms", 0.0, 150));
        break;
      }
    return paths;
  }
};

} // namespace ns3

#endif // LINK_PROFILE_H
