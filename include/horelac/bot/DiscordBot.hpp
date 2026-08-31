#pragma once
#include "horelac/services/ScheduleService.hpp"
#include "horelac/render/ScheduleRenderer.hpp"
#include "horelac/support/Config.hpp"
#include "horelac/support/Localization.hpp"
#include <dpp/dpp.h>
#include <memory>
namespace horelac::bot {
class DiscordBot {
  public:
    DiscordBot(const support::Config&,services::ScheduleService&,render::IScheduleRenderer&);
    void run();
  private:
    const support::Config& config_;services::ScheduleService& schedules_;render::IScheduleRenderer& renderer_;support::Localization localization_;dpp::cluster cluster_;
    void register_handlers();
    void register_commands();
    void handle_schedule(const dpp::slashcommand_t& event);
    void handle_button(const dpp::button_click_t& event);
    void handle_form(const dpp::form_submit_t& event);
    dpp::message public_message(domain::CalendarId,std::size_t);
    void update_public_message(domain::CalendarId,std::optional<std::size_t> week=std::nullopt);
    static std::string friendly_error(const std::exception& error);
};
} // namespace horelac::bot
