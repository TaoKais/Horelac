#include "horelac/support/Config.hpp"

#include <charconv>
#include <cstdlib>
#include <stdexcept>

namespace horelac::support {
namespace {
std::string env_or(const char* name, std::string fallback = {}) {
    const char* value = std::getenv(name);
    return value ? value : std::move(fallback);
}
std::uint64_t parse_snowflake(const std::string& value, const char* name) {
    std::uint64_t result{};
    const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), result);
    if (ec != std::errc{} || ptr != value.data() + value.size() || result == 0) {
        throw std::runtime_error(std::string("Invalid ") + name);
    }
    return result;
}
} // namespace
Config Config::from_environment() {
    Config c;
    c.discord_token = env_or("DISCORD_TOKEN");
    const auto app = env_or("DISCORD_APPLICATION_ID");
    if (c.discord_token.empty() || app.empty()) {
        throw std::runtime_error("DISCORD_TOKEN and DISCORD_APPLICATION_ID are required");
    }
    c.application_id = parse_snowflake(app, "DISCORD_APPLICATION_ID");
    if (const auto dev = env_or("DISCORD_DEV_GUILD_ID"); !dev.empty()) {
        c.development_guild_id = parse_snowflake(dev, "DISCORD_DEV_GUILD_ID");
    }
    c.database_path = env_or("DATABASE_PATH", "./data/horelac.db");
    c.log_level = env_or("LOG_LEVEL", "info");
    c.default_locale = env_or("DEFAULT_LOCALE", "en");
    c.render_output_dir = env_or("RENDER_OUTPUT_DIR", "./rendered");
    return c;
}
} // namespace horelac::support

