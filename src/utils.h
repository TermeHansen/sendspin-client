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

#pragma once

#include <string>

/// @brief Generate a unique client ID based on hardware information
/// @param friendly_name Friendly name to use as base (if available)
/// @return Unique client ID string
std::string generate_client_id(const std::string& friendly_name = "");

/// @brief Get hardware MAC address as a string
/// @return MAC address string or empty string if not available
std::string get_mac_address();

/// @brief Get system serial number (if available)
/// @return Serial number string or empty string if not available
std::string get_system_serial();