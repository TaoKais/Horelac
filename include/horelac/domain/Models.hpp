#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace horelac::domain {

using CalendarId = std::int64_t;
using ParticipantId = std::int64_t;
using IntervalId = std::int64_t;
using EventId = std::int64_t;
using Snowflake = std::uint64_t;
using LocalDate = std::chrono::year_month_day;

enum class CalendarState { open, closed, archived };
enum class IdentityMode { anonymous, alias, discord_name };
enum class AttendanceResponse { attending, maybe, cannot_attend };
enum class WeekStart { monday, sunday };
enum class CellValueMode { none, count, fraction, percentage };

struct CalendarConfig {
    std::chrono::year_month month;
    std::string timezone{"UTC"};
    int start_minute{18 * 60};
    int end_minute{23 * 60};
    int slot_minutes{30};
    WeekStart week_start{WeekStart::monday};
    std::uint8_t allowed_identity_modes{1};
    IdentityMode default_identity_mode{IdentityMode::anonymous};
    CellValueMode cell_value_mode{CellValueMode::none};
};

struct Calendar {
    CalendarId id{};
    Snowflake guild_id{};
    Snowflake channel_id{};
    Snowflake creator_user_id{};
    std::string title;
    std::string description;
    std::string locale{"en"};
    CalendarConfig config;
    CalendarState state{CalendarState::open};
    std::int64_t revision{};
};

struct Participant {
    ParticipantId id{};
    CalendarId calendar_id{};
    Snowflake discord_user_id{};
    IdentityMode identity_mode{IdentityMode::anonymous};
    std::optional<std::string> alias;
    std::optional<std::string> display_name;
};

struct AvailabilityInterval {
    IntervalId id{};
    CalendarId calendar_id{};
    ParticipantId participant_id{};
    LocalDate date;
    int start_minute{};
    int end_minute{};
};

struct HeatmapCell {
    LocalDate date;
    int start_minute{};
    int end_minute{};
    int available_count{};
    int total_participants{};

    [[nodiscard]] double percentage() const noexcept {
        return total_participants == 0
                   ? 0.0
                   : 100.0 * static_cast<double>(available_count) /
                         static_cast<double>(total_participants);
    }
};

struct WeeklyHeatmap {
    LocalDate first_date;
    LocalDate last_date;
    int slot_minutes{};
    int total_participants{};
    std::vector<HeatmapCell> cells;
};

struct BestTimeWindow {
    LocalDate date;
    int start_minute{};
    int end_minute{};
    int minimum_available{};
    double average_available{};
    double variance{};
    int total_participants{};
};

struct Event {
    EventId id{};
    std::optional<CalendarId> source_calendar_id;
    Snowflake guild_id{};
    Snowflake channel_id{};
    Snowflake creator_user_id{};
    std::string title;
    std::string description;
    std::chrono::sys_seconds starts_at_utc;
    std::string timezone;
    int duration_minutes{};
};

struct AttendanceRecord {
    EventId event_id{};
    Snowflake discord_user_id{};
    AttendanceResponse response{AttendanceResponse::attending};
};

} // namespace horelac::domain

