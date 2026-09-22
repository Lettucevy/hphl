#pragma once

#include <string>
#include <map>
#include <vector>

namespace hphl {

class MessageLoader {
public:
    MessageLoader();
    ~MessageLoader() = default;

    // Load messages from JSON file
    bool load(const std::string& path);

    // Get message by key, with optional parameter substitution
    std::string get(const std::string& key) const;

    // Get message with parameter substitution
    std::string get(const std::string& key, const std::vector<std::string>& params) const;

    // Set the current language/locale
    void setLocale(const std::string& locale);

    // Get current locale
    std::string getLocale() const;

    // Load default messages for locale
    bool loadDefault();

private:
    // Values stored as strings; JSON parser is a tiny stub (see .cpp).
    std::map<std::string, std::string> messages_;
    std::string locale_;

    // Load messages for current locale
    bool loadLocaleMessages();

    // Substitute parameters in message string
    // Parameters are referenced as {0}, {1}, etc.
    std::string substituteParams(const std::string& msg,
                                  const std::vector<std::string>& params) const;
};

// Global message loader instance (M30 v0.89.0: i18n foundation).
// ODR-safe: 'inline' allows this definition to be included in multiple TUs.
inline MessageLoader& messages() {
    static MessageLoader instance;
    return instance;
}

} // namespace hphl