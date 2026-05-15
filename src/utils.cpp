// Copyright 2026 TermeHansen
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "utils.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iomanip>

#ifdef __linux__
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace {

// Simple string cleaning function to make IDs URL-safe
std::string clean_string(const std::string& input) {
    std::string result = input;
    
    // Replace spaces and special characters with dashes
    for (char& c : result) {
        if (!isalnum(c) && c != '-' && c != '_') {
            c = '-';
        }
    }
    
    // Remove consecutive dashes
    size_t pos;
    while ((pos = result.find("--")) != std::string::npos) {
        result.replace(pos, 2, "-");
    }
    
    // Remove leading/trailing dashes
    if (!result.empty() && result[0] == '-') {
        result.erase(0, 1);
    }
    if (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    
    // Convert to lowercase
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    return result;
}

}  // namespace

std::string get_mac_address() {
#ifdef __linux__
    struct ifaddrs* ifaddr = nullptr;
    struct ifaddrs* ifa = nullptr;
    char mac_addr[18] = {0};  // XX:XX:XX:XX:XX:XX\0
    
    if (getifaddrs(&ifaddr) == -1) {
        return "";
    }
    
    // Look for first non-loopback interface with MAC address
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_PACKET) {
            continue;
        }
        
        struct sockaddr* sa = ifa->ifa_addr;
        if (sa->sa_data[0] == 0x00 && sa->sa_data[1] == 0x00 && 
            sa->sa_data[2] == 0x00 && sa->sa_data[3] == 0x00 &&
            sa->sa_data[4] == 0x00 && sa->sa_data[5] == 0x00) {
            continue;  // Skip null MAC addresses
        }
        
        snprintf(mac_addr, sizeof(mac_addr), "%02x:%02x:%02x:%02x:%02x:%02x",
                 (unsigned char)sa->sa_data[0], (unsigned char)sa->sa_data[1],
                 (unsigned char)sa->sa_data[2], (unsigned char)sa->sa_data[3],
                 (unsigned char)sa->sa_data[4], (unsigned char)sa->sa_data[5]);
        
        break;
    }
    
    freeifaddrs(ifaddr);
    
    if (mac_addr[0] != '\0') {
        return std::string(mac_addr);
    }
#endif
    
    return "";
}

std::string get_system_serial() {
    // Try common locations for system serial number
    const char* serial_files[] = {
        "/sys/class/dmi/id/product_serial",
        "/proc/cpuinfo",  // Raspberry Pi
        "/etc/machine-id",
        nullptr
    };
    
    for (const char** file = serial_files; *file != nullptr; ++file) {
        std::ifstream f(*file);
        if (f) {
            std::string line;
            if (std::getline(f, line)) {
                // For /proc/cpuinfo on Raspberry Pi, extract Serial line
                if (strstr(*file, "cpuinfo") && line.find("Serial") != std::string::npos) {
                    size_t colon_pos = line.find(':');
                    if (colon_pos != std::string::npos) {
                        line = line.substr(colon_pos + 1);
                    }
                    // Trim whitespace
                    line.erase(0, line.find_first_not_of(" \t"));
                    line.erase(line.find_last_not_of(" \t") + 1);
                    if (!line.empty()) {
                        return line;
                    }
                } else {
                    // For other files, use the first line
                    line.erase(line.find_last_not_of(" \t\n\r") + 1);
                    if (!line.empty()) {
                        return line;
                    }
                }
            }
        }
    }
    
    return "";
}

std::string generate_client_id(const std::string& friendly_name) {
    std::string base_id;
    
    // Use friendly name as base if provided
    if (!friendly_name.empty()) {
        base_id = clean_string(friendly_name);
    } else {
        base_id = "sendspin-client";
    }
    
    // Try to get hardware identifiers
    std::string mac_addr = get_mac_address();
    std::string serial = get_system_serial();
    
    // Build unique client ID
    if (!mac_addr.empty()) {
        // Replace colons in MAC address for cleaner ID
        std::string clean_mac = mac_addr;
        std::replace(clean_mac.begin(), clean_mac.end(), ':', '-');
        return base_id + "-" + clean_mac;
    } else if (!serial.empty()) {
        return base_id + "-" + clean_string(serial);
    } else {
        // Fallback: use friendly name with a random suffix
        // This ensures uniqueness even without hardware info
        return base_id + "-" + std::to_string(std::hash<std::string>{}(base_id));
    }
}