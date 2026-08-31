#include "horelac/bot/DiscordBot.hpp"
#include "horelac/domain/Errors.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <format>
#include <iostream>
#include <variant>

namespace horelac::bot {
namespace {

std::int64_t integer(const dpp::slashcommand_t& e, const char* name,
                     std::int64_t fallback = 0) {
    const auto v = e.get_parameter(name);
    return std::holds_alternative<std::int64_t>(v) ? std::get<std::int64_t>(v) : fallback;
}

std::string string(const dpp::slashcommand_t& e, const char* name,
                   std::string fallback = {}) {
    const auto v = e.get_parameter(name);
    return std::holds_alternative<std::string>(v) ? std::get<std::string>(v)
                                                   : std::move(fallback);
}

std::optional<std::int64_t> parse_id(std::string_view id, std::string_view action) {
    const auto prefix = std::format("hsb:v2:{}:", action);
    if (!id.starts_with(prefix)) {
        return std::nullopt;
    }
    std::int64_t result{};
    const auto raw = id.substr(prefix.size());
    const auto [p, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), result);
    return ec == std::errc{} && p == raw.data() + raw.size() ? std::optional{result}
                                                             : std::nullopt;
}

dpp::component button(std::string label, std::string id,
                      dpp::component_style style = dpp::cos_primary) {
    return dpp::component()
        .set_type(dpp::cot_button)
        .set_style(style)
        .set_label(label)
        .set_id(id);
}

domain::LocalDate parse_date(std::string_view value) {
    int y{}, m{}, d{};
    if (std::sscanf(std::string(value).c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        throw domain::DomainError(domain::ErrorCode::invalid_input,
                                  "Date must be YYYY-MM-DD");
    }
    domain::LocalDate result{std::chrono::year{y},
                             std::chrono::month{static_cast<unsigned>(m)},
                             std::chrono::day{static_cast<unsigned>(d)}};
    if (!result.ok()) {
        throw domain::DomainError(domain::ErrorCode::invalid_input, "Date is invalid");
    }
    return result;
}

int parse_time(std::string_view value) {
    int h{}, m{};
    if (std::sscanf(std::string(value).c_str(), "%d:%d", &h, &m) != 2 || h < 0 || h > 24 ||
        m < 0 || m > 59 || (h == 24 && m != 0)) {
        throw domain::DomainError(domain::ErrorCode::invalid_time_range,
                                  "Time must be HH:MM (00:00 through 24:00)");
    }
    return h * 60 + m;
}

std::string form_value(const dpp::form_submit_t& event, std::string_view id) {
    for (const auto& row : event.components) {
        for (const auto& component : row.components) {
            if (component.custom_id == id &&
                std::holds_alternative<std::string>(component.value)) {
                return std::get<std::string>(component.value);
            }
        }
    }
    throw domain::DomainError(domain::ErrorCode::invalid_input, "Missing form value");
}

std::string date_text(domain::LocalDate d) {
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(d.year()),
                       static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

std::string time_text(int minute) {
    return std::format("{:02}:{:02}", minute / 60, minute % 60);
}

std::string month_text(const domain::Calendar& c) {
    static constexpr std::array names{"January",   "February", "March",    "April",
                                      "May",       "June",     "July",     "August",
                                      "September", "October",  "November", "December"};
    return std::format("{} {}", names[static_cast<unsigned>(c.config.month.month()) - 1],
                       static_cast<int>(c.config.month.year()));
}

std::string png_string(const std::vector<std::uint8_t>& bytes) {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

std::string interaction_display_name(const dpp::interaction& interaction) {
    const auto nickname = interaction.member.get_nickname();
    if (!nickname.empty()) {
        return nickname;
    }
    return interaction.get_issuing_user().format_username();
}

std::string participant_name(const domain::Participant& participant) {
    if (participant.display_name && !participant.display_name->empty()) {
        return *participant.display_name;
    }
    if (auto* cached = dpp::find_user(participant.discord_user_id)) {
        return cached->format_username();
    }
    return std::format("Discord user {}", participant.discord_user_id);
}

std::string attendees_text(const domain::Calendar& calendar, domain::CalendarId cid,
                           domain::LocalDate date,
                           const std::vector<domain::Participant>& participants) {
    std::string response = std::format("**{} · available on {}**\nCalendar #{}\n",
                                       calendar.title, date_text(date), cid);
    if (participants.empty()) {
        response += "Nobody has submitted availability for this day.";
    } else {
        for (const auto& participant : participants) {
            response += std::format("• {}\n", participant_name(participant));
        }
    }
    return response;
}

} // namespace

DiscordBot::DiscordBot(const support::Config& config, services::ScheduleService& service,
                       render::IScheduleRenderer& renderer)
    : config_(config), schedules_(service), renderer_(renderer),
      localization_(config.default_locale),
      cluster_(config.discord_token, dpp::i_default_intents) {
    register_handlers();
}

void DiscordBot::run() {
    cluster_.start(dpp::st_wait);
}

void DiscordBot::register_handlers() {
    cluster_.on_log([](const dpp::log_t& e) {
        if (e.severity >= dpp::ll_info) {
            std::cerr << e.message << '\n';
        }
    });
    cluster_.on_ready([this](const dpp::ready_t&) {
        if (dpp::run_once<struct register_bot_commands>()) {
            register_commands();
        }
        std::cerr << "Horelac connected\n";
    });
    cluster_.on_slashcommand([this](const dpp::slashcommand_t& e) {
        if (e.command.get_command_name() == "schedule") {
            handle_schedule(e);
        }
    });
    cluster_.on_button_click([this](const dpp::button_click_t& e) { handle_button(e); });
    cluster_.on_form_submit([this](const dpp::form_submit_t& e) { handle_form(e); });
}

void DiscordBot::register_commands() {
    dpp::slashcommand command("schedule", "Create and manage availability calendars",
                              config_.application_id);
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "create", "Create a calendar")
            .add_option(dpp::command_option(dpp::co_string, "name", "Calendar name", true)
                            .set_max_length(100))
            .add_option(dpp::command_option(dpp::co_string, "month", "Month as YYYY-MM", true)
                            .set_max_length(7))
            .add_option(dpp::command_option(dpp::co_string, "timezone", "IANA timezone", true)
                            .set_max_length(64))
            .add_option(dpp::command_option(dpp::co_integer, "start", "Start hour (0-23)", true)
                            .set_min_value(0)
                            .set_max_value(23))
            .add_option(dpp::command_option(dpp::co_integer, "end", "End hour (1-24)", true)
                            .set_min_value(1)
                            .set_max_value(24)));
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "add", "Add availability")
            .add_option(dpp::command_option(dpp::co_integer, "calendar", "Calendar ID", true))
            .add_option(dpp::command_option(dpp::co_string, "date", "Date as YYYY-MM-DD", true)
                            .set_max_length(10))
            .add_option(dpp::command_option(dpp::co_string, "start", "Start time as HH:MM", true)
                            .set_max_length(5))
            .add_option(dpp::command_option(dpp::co_string, "end", "End time as HH:MM", true)
                            .set_max_length(5)));
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "view", "View a calendar week")
            .add_option(dpp::command_option(dpp::co_integer, "calendar", "Calendar ID", true))
            .add_option(dpp::command_option(dpp::co_integer, "week", "Week number", false)
                            .set_min_value(1)
                            .set_max_value(6)));
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "best", "Find best continuous windows")
            .add_option(dpp::command_option(dpp::co_integer, "calendar", "Calendar ID", true))
            .add_option(dpp::command_option(dpp::co_integer, "duration", "Duration in minutes", true)
                            .set_min_value(15)
                            .set_max_value(720))
            .add_option(dpp::command_option(dpp::co_integer, "week", "Week number", false)
                            .set_min_value(1)
                            .set_max_value(6)));
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "attendees",
                            "List members available on a day (calendar owner only)")
            .add_option(dpp::command_option(dpp::co_integer, "calendar", "Calendar ID", true))
            .add_option(dpp::command_option(dpp::co_string, "date", "Date as YYYY-MM-DD", true)
                            .set_max_length(10)));
    command.add_option(
        dpp::command_option(dpp::co_sub_command, "clear", "Delete your data in a calendar")
            .add_option(dpp::command_option(dpp::co_integer, "calendar", "Calendar ID", true)));

    if (config_.development_guild_id) {
        cluster_.guild_command_create(command, *config_.development_guild_id);
    } else {
        cluster_.global_command_create(command);
    }
}

dpp::message DiscordBot::public_message(domain::CalendarId cid, std::size_t week) {
    const auto calendar = schedules_.calendar(cid);
    const auto count = schedules_.week_count(cid);
    const auto heatmap = schedules_.weekly_heatmap(cid, week);
    const auto rendered = renderer_.render_weekly(calendar, heatmap, week, count);

    dpp::message message(std::format(
        "**{}** · Calendar #{}\n{} · {}\nWeek {} of {} · {} participant{}", calendar.title,
        cid, month_text(calendar), calendar.config.timezone, week + 1, count,
        heatmap.total_participants, heatmap.total_participants == 1 ? "" : "s"));
    message.add_file("availability.png", png_string(rendered.png), "image/png");
    message.add_component(
        dpp::component()
            .add_component(button("Add Availability", std::format("hsb:v2:add:{}", cid)))
            .add_component(button("Previous Week", std::format("hsb:v2:prev:{}", cid),
                                  dpp::cos_secondary))
            .add_component(button("Next Week", std::format("hsb:v2:next:{}", cid),
                                  dpp::cos_secondary))
            .add_component(button("My Schedule", std::format("hsb:v2:mine:{}", cid),
                                  dpp::cos_secondary))
            .add_component(button("Best Times", std::format("hsb:v2:best:{}", cid),
                                  dpp::cos_success)));
    message.add_component(dpp::component().add_component(
        button("Attendees", std::format("hsb:v2:attendees:{}", cid), dpp::cos_secondary)));

    std::cerr << "Rendered anonymous calendar " << cid << " week " << week + 1 << '\n';
    return message;
}

void DiscordBot::update_public_message(domain::CalendarId cid,
                                       std::optional<std::size_t> requested) {
    const auto ref = schedules_.calendar_message(cid);
    if (!ref) {
        throw domain::DomainError(domain::ErrorCode::invalid_input,
                                  "Calendar message not found");
    }
    const auto week = requested.value_or(ref->displayed_week);
    auto message = public_message(cid, week);
    message.channel_id = ref->channel_id;
    message.id = ref->message_id;
    cluster_.message_edit(message, [cid](const dpp::confirmation_callback_t& result) {
        std::cerr << (result.is_error() ? "Discord calendar message update failed for "
                                        : "Updated Discord calendar message for ")
                  << cid << '\n';
    });
    auto updated = *ref;
    updated.displayed_week = week;
    schedules_.set_calendar_message(updated);
}

void DiscordBot::handle_schedule(const dpp::slashcommand_t& event) {
    try {
        const auto sub = event.command.get_command_interaction().options.at(0).name;
        const auto guild = static_cast<domain::Snowflake>(event.command.guild_id);
        const auto channel = static_cast<domain::Snowflake>(event.command.channel_id);
        const auto user = static_cast<domain::Snowflake>(event.command.get_issuing_user().id);
        const auto display_name = interaction_display_name(event.command);

        if (sub == "create") {
            const auto month = string(event, "month");
            int y{}, m{};
            if (std::sscanf(month.c_str(), "%d-%d", &y, &m) != 2) {
                throw domain::DomainError(domain::ErrorCode::invalid_input,
                                          "Month must be YYYY-MM");
            }
            domain::CalendarConfig cfg;
            cfg.month = std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)};
            cfg.timezone = string(event, "timezone");
            cfg.start_minute = static_cast<int>(integer(event, "start")) * 60;
            cfg.end_minute = static_cast<int>(integer(event, "end")) * 60;
            cfg.allowed_identity_modes =
                1U << static_cast<unsigned>(domain::IdentityMode::anonymous);
            cfg.default_identity_mode = domain::IdentityMode::anonymous;
            cfg.cell_value_mode = domain::CellValueMode::fraction;
            const auto calendar = schedules_.create_calendar(
                {guild, channel, user, string(event, "name"), "", config_.default_locale, cfg});
            event.reply(public_message(calendar.id, 0),
                        [this, event, calendar](const dpp::confirmation_callback_t& result) {
                            if (result.is_error()) {
                                std::cerr << "Discord calendar message creation failed for "
                                          << calendar.id << '\n';
                                return;
                            }
                            event.get_original_response(
                                [this, calendar](const dpp::confirmation_callback_t& response) {
                                    if (response.is_error()) {
                                        std::cerr
                                            << "Could not persist Discord message reference for "
                                            << calendar.id << '\n';
                                        return;
                                    }
                                    const auto& created = std::get<dpp::message>(response.value);
                                    schedules_.set_calendar_message(
                                        {calendar.id, calendar.guild_id, calendar.channel_id,
                                         static_cast<domain::Snowflake>(created.id), 0});
                                });
                        });
        } else if (sub == "add") {
            const auto cid = integer(event, "calendar");
            schedules_.add_availability(
                {cid, user, domain::IdentityMode::anonymous, std::nullopt, display_name,
                 {parse_date(string(event, "date"))}, parse_time(string(event, "start")),
                 parse_time(string(event, "end"))});
            event.reply(dpp::message("Availability saved.").set_flags(dpp::m_ephemeral));
            update_public_message(cid);
        } else if (sub == "view") {
            const auto cid = integer(event, "calendar");
            const auto week = static_cast<std::size_t>(
                std::max<std::int64_t>(1, integer(event, "week", 1)) - 1);
            event.reply(public_message(cid, week).set_flags(dpp::m_ephemeral));
        } else if (sub == "best") {
            const auto cid = integer(event, "calendar");
            const auto week = static_cast<std::size_t>(
                std::max<std::int64_t>(1, integer(event, "week", 1)) - 1);
            const auto windows = schedules_.best_windows(
                cid, week, static_cast<int>(integer(event, "duration")));
            std::string response = "**Best aggregate windows**\n";
            for (const auto& w : windows) {
                response += std::format("{} {}–{} · {}/{} available\n", date_text(w.date),
                                        time_text(w.start_minute), time_text(w.end_minute),
                                        w.minimum_available, w.total_participants);
            }
            event.reply(dpp::message(response).set_flags(dpp::m_ephemeral));
        } else if (sub == "attendees") {
            const auto cid = integer(event, "calendar");
            const auto date = parse_date(string(event, "date"));
            const auto participants = schedules_.day_participants(cid, date, user);
            const auto calendar = schedules_.calendar(cid);
            event.reply(dpp::message(attendees_text(calendar, cid, date, participants))
                            .set_flags(dpp::m_ephemeral));
        } else if (sub == "clear") {
            const auto cid = integer(event, "calendar");
            schedules_.clear_my_data(cid, user);
            event.reply(dpp::message("Your scheduling data was removed.")
                            .set_flags(dpp::m_ephemeral));
            update_public_message(cid);
        }
    } catch (const std::exception& error) {
        event.reply(dpp::message(friendly_error(error)).set_flags(dpp::m_ephemeral));
    }
}

void DiscordBot::handle_button(const dpp::button_click_t& event) {
    try {
        if (auto cid = parse_id(event.custom_id, "add")) {
            dpp::interaction_modal_response modal(std::format("hsb:v2:submit:{}", *cid),
                                                  "Add availability");
            modal.add_component(dpp::component()
                                    .set_label("Date")
                                    .set_id("date")
                                    .set_type(dpp::cot_text)
                                    .set_placeholder("2026-09-05")
                                    .set_min_length(10)
                                    .set_max_length(10)
                                    .set_text_style(dpp::text_short));
            modal.add_row().add_component(dpp::component()
                                              .set_label("Start time")
                                              .set_id("start")
                                              .set_type(dpp::cot_text)
                                              .set_placeholder("18:00")
                                              .set_min_length(5)
                                              .set_max_length(5)
                                              .set_text_style(dpp::text_short));
            modal.add_row().add_component(dpp::component()
                                              .set_label("End time")
                                              .set_id("end")
                                              .set_type(dpp::cot_text)
                                              .set_placeholder("22:30")
                                              .set_min_length(5)
                                              .set_max_length(5)
                                              .set_text_style(dpp::text_short));
            event.dialog(modal);
        } else if (auto cid = parse_id(event.custom_id, "attendees")) {
            dpp::interaction_modal_response modal(
                std::format("hsb:v2:attendees-submit:{}", *cid), "View attendees");
            modal.add_component(dpp::component()
                                    .set_label("Date")
                                    .set_id("date")
                                    .set_type(dpp::cot_text)
                                    .set_placeholder("2026-09-02")
                                    .set_min_length(10)
                                    .set_max_length(10)
                                    .set_text_style(dpp::text_short));
            event.dialog(modal);
        } else if (auto cid = parse_id(event.custom_id, "mine")) {
            const auto user =
                static_cast<domain::Snowflake>(event.command.get_issuing_user().id);
            const auto intervals = schedules_.my_schedule(*cid, user);
            std::string response = intervals.empty() ? "You have not submitted availability."
                                                     : "**Your schedule**\n";
            for (const auto& i : intervals) {
                response += std::format("{} · {}–{}\n", date_text(i.date),
                                        time_text(i.start_minute), time_text(i.end_minute));
            }
            event.reply(dpp::message(response).set_flags(dpp::m_ephemeral));
        } else if (auto cid = parse_id(event.custom_id, "prev")) {
            const auto ref = schedules_.calendar_message(*cid);
            if (!ref) {
                throw domain::DomainError(domain::ErrorCode::invalid_input,
                                          "Calendar message not found");
            }
            event.reply(dpp::message("Showing previous week.").set_flags(dpp::m_ephemeral));
            update_public_message(*cid, ref->displayed_week == 0 ? 0 : ref->displayed_week - 1);
        } else if (auto cid = parse_id(event.custom_id, "next")) {
            const auto ref = schedules_.calendar_message(*cid);
            if (!ref) {
                throw domain::DomainError(domain::ErrorCode::invalid_input,
                                          "Calendar message not found");
            }
            event.reply(dpp::message("Showing next week.").set_flags(dpp::m_ephemeral));
            update_public_message(
                *cid, std::min(ref->displayed_week + 1, schedules_.week_count(*cid) - 1));
        } else if (auto cid = parse_id(event.custom_id, "best")) {
            const auto ref = schedules_.calendar_message(*cid);
            const auto windows = schedules_.best_windows(*cid, ref ? ref->displayed_week : 0, 60);
            std::string response = "**Best aggregate times (60 minutes)**\n";
            for (const auto& w : windows) {
                response += std::format("{} · {}–{} · {}/{} available\n", date_text(w.date),
                                        time_text(w.start_minute), time_text(w.end_minute),
                                        w.minimum_available, w.total_participants);
            }
            event.reply(dpp::message(response).set_flags(dpp::m_ephemeral));
        } else {
            event.reply(dpp::message("This control is unavailable or invalid.")
                            .set_flags(dpp::m_ephemeral));
        }
    } catch (const std::exception& error) {
        event.reply(dpp::message(friendly_error(error)).set_flags(dpp::m_ephemeral));
    }
}

void DiscordBot::handle_form(const dpp::form_submit_t& event) {
    try {
        if (const auto cid = parse_id(event.custom_id, "submit")) {
            const auto user =
                static_cast<domain::Snowflake>(event.command.get_issuing_user().id);
            const auto display_name = interaction_display_name(event.command);
            schedules_.add_availability(
                {*cid, user, domain::IdentityMode::anonymous, std::nullopt, display_name,
                 {parse_date(form_value(event, "date"))},
                 parse_time(form_value(event, "start")), parse_time(form_value(event, "end"))});
            event.reply(dpp::message("Availability saved.").set_flags(dpp::m_ephemeral));
            update_public_message(*cid);
            return;
        }

        if (const auto cid = parse_id(event.custom_id, "attendees-submit")) {
            const auto user =
                static_cast<domain::Snowflake>(event.command.get_issuing_user().id);
            const auto date = parse_date(form_value(event, "date"));
            const auto participants = schedules_.day_participants(*cid, date, user);
            const auto calendar = schedules_.calendar(*cid);
            event.reply(dpp::message(attendees_text(calendar, *cid, date, participants))
                            .set_flags(dpp::m_ephemeral));
        }
    } catch (const std::exception& error) {
        event.reply(dpp::message(friendly_error(error)).set_flags(dpp::m_ephemeral));
    }
}

std::string DiscordBot::friendly_error(const std::exception& error) {
    if (const auto* e = dynamic_cast<const domain::DomainError*>(&error)) {
        switch (e->code()) {
        case domain::ErrorCode::calendar_not_found:
            return "Calendar not found.";
        case domain::ErrorCode::permission_denied:
            return "You are not allowed to do that.";
        case domain::ErrorCode::calendar_closed:
            return "This calendar is closed.";
        case domain::ErrorCode::alias_already_used:
            return "That alias is already in use.";
        default:
            return e->what();
        }
    }
    return "The request could not be completed.";
}

} // namespace horelac::bot
