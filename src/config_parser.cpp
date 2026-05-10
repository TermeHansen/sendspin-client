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

#include "config_parser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

bool ConfigParser::parse(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (is_comment_or_empty(line)) {
            continue;
        }

        auto [key, value] = parse_line(line);
        if (!key.empty()) {
            config_map_[key] = value;
        }
    }

    return true;
}

std::string ConfigParser::get_string(const std::string& key, const std::string& default_value) const {
    auto it = config_map_.find(key);
    if (it != config_map_.end()) {
        return it->second;
    }
    return default_value;
}

int ConfigParser::get_int(const std::string& key, int default_value) const {
    std::string value = get_string(key, "");
    if (value.empty()) {
        return default_value;
    }
    
    try {
        return std::stoi(value);
    } catch (...) {
        return default_value;
    }
}

bool ConfigParser::get_bool(const std::string& key, bool default_value) const {
    std::string value = get_string(key, "");
    if (value.empty()) {
        return default_value;
    }
    
    // Convert to lowercase for case-insensitive comparison
    std::string lower_value = value;
    std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    
    return (lower_value == "true" || lower_value == "yes" || lower_value == "1" || lower_value == "on");
}

std::vector<std::string> ConfigParser::get_string_list(const std::string& key) const {
    std::vector<std::string> result;
    std::string value = get_string(key, "");
    
    if (value.empty()) {
        return result;
    }
    
    std::istringstream iss(value);
    std::string item;
    while (std::getline(iss, item, ',')) {
        std::string trimmed = trim(item);
        if (!trimmed.empty()) {
            result.push_back(trimmed);
        }
    }
    
    return result;
}

bool ConfigParser::has_key(const std::string& key) const {
    return config_map_.find(key) != config_map_.end();
}

std::vector<std::string> ConfigParser::get_all_keys() const {
    std::vector<std::string> keys;
    for (const auto& pair : config_map_) {
        keys.push_back(pair.first);
    }
    return keys;
}

void ConfigParser::clear() {
    config_map_.clear();
}

void ConfigParser::set(const std::string& key, const std::string& value) {
    config_map_[key] = value;
}

std::string ConfigParser::trim(const std::string& str) const {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}

bool ConfigParser::is_comment_or_empty(const std::string& line) const {
    std::string trimmed = trim(line);
    return trimmed.empty() || trimmed[0] == '#';
}

std::pair<std::string, std::string> ConfigParser::parse_line(const std::string& line) const {
    size_t delimiter_pos = line.find('=');
    if (delimiter_pos == std::string::npos) {
        return {"", ""};
    }
    
    std::string key = trim(line.substr(0, delimiter_pos));
    std::string value = trim(line.substr(delimiter_pos + 1));
    
    // Remove quotes if present
    if (!value.empty() && value.front() == '"' && value.back() == '"') {
        value = value.substr(1, value.length() - 2);
    }
    if (!value.empty() && value.front() == '\'' && value.back() == '\'') {
        value = value.substr(1, value.length() - 2);
    }
    
    return {key, value};
}
