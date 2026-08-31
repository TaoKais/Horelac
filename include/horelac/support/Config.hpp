#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace horelac::support {

struct Config {
    std::string discord_token;
    std::uint64_t application_id{};
    std::optional<std::uint64_t> development_guild_id;
    std::string database_path{"./data/horelac.db"};
    std::string log_level{"info"};
    std::string default_locale{"en"};
    std::string render_output_dir{"./rendered"};
    static Config from_environment();
};

} // namespace horelac::support

