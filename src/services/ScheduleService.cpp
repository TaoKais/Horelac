#include "horelac/services/ScheduleService.hpp"

#include "horelac/domain/Errors.hpp"

#include <algorithm>
#include <cctype>
#include <set>

namespace horelac::services {
namespace {

void validate_text(const std::string& value, std::size_t minimum, std::size_t maximum,
                   const char* field) {
    if (value.size() < minimum || value.size() > maximum) {
        throw domain::DomainError(domain::ErrorCode::invalid_input,
                                  std::string(field) + " has invalid length");
    }
}

bool mode_allowed(const domain::CalendarConfig& config, domain::IdentityMode mode) {
    return (config.allowed_identity_modes & (1U << static_cast<unsigned>(mode))) != 0;
}

} // namespace

ScheduleService::ScheduleService(persistence::IScheduleRepository& repository)
    : repository_(repository) {}

domain::Calendar ScheduleService::create_calendar(const CreateCalendarRequest& request) {
    validate_text(request.title, 1, 100, "title");
    validate_text(request.description, 0, 1000, "description");
    if (!domain::is_valid_calendar_config(request.config)) {
        throw domain::DomainError(domain::ErrorCode::invalid_input,
                                  "Invalid calendar configuration");
    }
    domain::Calendar calendar{0, request.guild_id, request.channel_id, request.actor_user_id,
                              request.title, request.description, request.locale, request.config};
    calendar.id = repository_.create_calendar(calendar);
    invalidate(calendar.id);
    return calendar;
}

void ScheduleService::add_availability(const AddAvailabilityRequest& request) {
    const auto calendar = require_calendar(request.calendar_id);
    if (calendar.state != domain::CalendarState::open) {
        throw domain::DomainError(domain::ErrorCode::calendar_closed, "Calendar is not open");
    }
    if (!mode_allowed(calendar.config, request.identity_mode)) {
        throw domain::DomainError(domain::ErrorCode::permission_denied,
                                  "Identity mode is not allowed");
    }
    if (request.identity_mode == domain::IdentityMode::alias) {
        if (!request.alias) {
            throw domain::DomainError(domain::ErrorCode::invalid_input, "Alias is required");
        }
        validate_text(*request.alias, 1, 32, "alias");
    }
    if (request.start_minute < calendar.config.start_minute ||
        request.end_minute > calendar.config.end_minute ||
        request.start_minute >= request.end_minute ||
        (request.start_minute - calendar.config.start_minute) % calendar.config.slot_minutes != 0 ||
        (request.end_minute - calendar.config.start_minute) % calendar.config.slot_minutes != 0) {
        throw domain::DomainError(domain::ErrorCode::invalid_time_range, "Invalid time range");
    }
    if (request.dates.empty()) {
        throw domain::DomainError(domain::ErrorCode::invalid_input, "At least one date is required");
    }
    for (const auto date : request.dates) {
        if (!domain::is_date_in_month(date, calendar.config.month)) {
            throw domain::DomainError(domain::ErrorCode::invalid_input,
                                      "Date is outside calendar month");
        }
    }
    domain::Participant participant{0, calendar.id, request.actor_user_id, request.identity_mode,
                                    request.alias, request.display_name};
    try {
        participant = repository_.upsert_participant(participant);
    } catch (const domain::DomainError& error) {
        if (error.code() == domain::ErrorCode::database_error && request.alias) {
            throw domain::DomainError(domain::ErrorCode::alias_already_used,
                                      "Alias is already used in this calendar");
        }
        throw;
    }
    std::set<domain::LocalDate> unique_dates(request.dates.begin(), request.dates.end());
    for (const auto date : unique_dates) {
        repository_.add_interval({0, calendar.id, participant.id, date, request.start_minute,
                                  request.end_minute});
    }
    invalidate(calendar.id);
}

domain::Calendar ScheduleService::calendar(domain::CalendarId calendar_id) {
    return require_calendar(calendar_id);
}

std::size_t ScheduleService::week_count(domain::CalendarId calendar_id) {
    const auto value = require_calendar(calendar_id);
    return domain::month_weeks(value.config.month, value.config.week_start).size();
}

std::vector<domain::AvailabilityInterval> ScheduleService::my_schedule(
    domain::CalendarId calendar_id, domain::Snowflake actor_user_id) {
    const auto value = require_calendar(calendar_id);
    const auto participant = repository_.find_participant(calendar_id, actor_user_id);
    if (!participant) {
        return {};
    }
    auto intervals = repository_.list_intervals(
        calendar_id, domain::LocalDate{value.config.month / std::chrono::day{1}},
        domain::LocalDate{value.config.month / std::chrono::last});
    std::erase_if(intervals, [&](const auto& interval) {
        return interval.participant_id != participant->id;
    });
    return intervals;
}

void ScheduleService::set_calendar_message(const domain::CalendarMessageReference& reference) {
    (void)require_calendar(reference.calendar_id);
    repository_.set_calendar_message(reference.calendar_id, reference.guild_id,
                                     reference.channel_id, reference.message_id,
                                     static_cast<int>(reference.displayed_week), false);
}

std::optional<domain::CalendarMessageReference> ScheduleService::calendar_message(
    domain::CalendarId calendar_id) {
    (void)require_calendar(calendar_id);
    return repository_.find_calendar_message(calendar_id);
}

void ScheduleService::clear_my_data(domain::CalendarId calendar_id,
                                    domain::Snowflake actor_user_id) {
    (void)require_calendar(calendar_id);
    repository_.clear_participant(calendar_id, actor_user_id);
    invalidate(calendar_id);
}

domain::WeeklyHeatmap ScheduleService::weekly_heatmap(domain::CalendarId calendar_id,
                                                       std::size_t week_index) {
    const auto calendar = require_calendar(calendar_id);
    const auto weeks = domain::month_weeks(calendar.config.month, calendar.config.week_start);
    if (week_index >= weeks.size()) {
        throw domain::DomainError(domain::ErrorCode::invalid_input, "Invalid week");
    }
    const auto [first, last] = weeks[week_index];
    auto intervals = repository_.list_intervals(calendar_id, first, last);
    return domain::aggregate_week(calendar, first, last, intervals,
                                  repository_.participant_count(calendar_id));
}

std::vector<domain::BestTimeWindow> ScheduleService::best_windows(
    domain::CalendarId calendar_id, std::size_t week_index, int duration_minutes,
    std::size_t limit) {
    return domain::rank_best_windows(weekly_heatmap(calendar_id, week_index), duration_minutes,
                                     limit);
}

std::vector<std::string> ScheduleService::public_participants(domain::CalendarId calendar_id) {
    const auto calendar = require_calendar(calendar_id);
    const auto participants = repository_.list_participants(calendar_id);
    std::vector<std::string> public_names;
    if (calendar.config.allowed_identity_modes ==
        (1U << static_cast<unsigned>(domain::IdentityMode::anonymous))) {
        return public_names;
    }
    for (const auto& participant : participants) {
        if (participant.identity_mode == domain::IdentityMode::alias && participant.alias) {
            public_names.push_back(*participant.alias);
        } else if (participant.identity_mode == domain::IdentityMode::discord_name &&
                   participant.display_name) {
            public_names.push_back(*participant.display_name);
        }
    }
    return public_names;
}

void ScheduleService::change_state(domain::CalendarId id, domain::Snowflake actor,
                                   domain::CalendarState state) {
    const auto calendar = require_calendar(id);
    if (calendar.creator_user_id != actor) {
        throw domain::DomainError(domain::ErrorCode::permission_denied, "Owner permission required");
    }
    repository_.update_calendar_state(id, state);
    invalidate(id);
}

void ScheduleService::delete_calendar(domain::CalendarId id, domain::Snowflake actor) {
    const auto calendar = require_calendar(id);
    if (calendar.creator_user_id != actor) {
        throw domain::DomainError(domain::ErrorCode::permission_denied, "Owner permission required");
    }
    repository_.delete_calendar(id);
    invalidate(id);
}

domain::Calendar ScheduleService::require_calendar(domain::CalendarId id) {
    {
        std::scoped_lock lock(cache_mutex_);
        if (auto it = calendar_cache_.find(id); it != calendar_cache_.end()) {
            return it->second;
        }
    }
    auto calendar = repository_.find_calendar(id);
    if (!calendar) {
        throw domain::DomainError(domain::ErrorCode::calendar_not_found, "Calendar not found");
    }
    std::scoped_lock lock(cache_mutex_);
    calendar_cache_[id] = *calendar;
    return *calendar;
}

void ScheduleService::invalidate(domain::CalendarId id) {
    std::scoped_lock lock(cache_mutex_);
    calendar_cache_.erase(id);
}

} // namespace horelac::services
