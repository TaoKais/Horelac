#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
namespace horelac::support {
class Localization {
  public:
    explicit Localization(std::string fallback = "en");
    [[nodiscard]] std::string text(std::string_view locale, std::string_view key) const;
  private:
    std::string fallback_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> values_;
};
} // namespace horelac::support

