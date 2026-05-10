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
#include <unordered_map>
#include <vector>

class ConfigParser {
public:
    /**
     * @brief Parse a configuration file
     * @param config_file Path to configuration file
     * @return true if parsing succeeded, false otherwise
     */
    bool parse(const std::string& config_file);

    /**
     * @brief Get a string configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value or default
     */
    std::string get_string(const std::string& key, const std::string& default_value = "") const;

    /**
     * @brief Get an integer configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value or default
     */
    int get_int(const std::string& key, int default_value = 0) const;

    /**
     * @brief Get a boolean configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value or default
     */
    bool get_bool(const std::string& key, bool default_value = false) const;

    /**
     * @brief Get a list of string values
     * @param key Configuration key
     * @return Vector of string values
     */
    std::vector<std::string> get_string_list(const std::string& key) const;

    /**
     * @brief Check if a key exists
     * @param key Configuration key
     * @return true if key exists
     */
    bool has_key(const std::string& key) const;

    /**
     * @brief Get all configuration keys
     * @return Vector of all keys
     */
    std::vector<std::string> get_all_keys() const;

    /**
     * @brief Clear all configuration
     */
    void clear();

    /**
     * @brief Set a configuration value programmatically
     * @param key Configuration key
     * @param value Configuration value
     */
    void set(const std::string& key, const std::string& value);

private:
    std::unordered_map<std::string, std::string> config_map_;

    // Helper methods
    std::string trim(const std::string& str) const;
    bool is_comment_or_empty(const std::string& line) const;
    std::pair<std::string, std::string> parse_line(const std::string& line) const;
};
