#pragma once

#include "horelac/domain/Models.hpp"

#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace horelac::persistence {

class IScheduleRepository {
  public:
    virtual ~IScheduleRepository() = default;
    virtual void migrate() = 0;
    virtual domain::CalendarId create_calendar(const domain::Calendar& calendar) = 0;
    virtual std::optional<domain::Calendar> find_calendar(domain::CalendarId id) = 0;
    virtual std::vector<domain::Calendar> list_open_calendars(domain::Snowflake guild_id,
                                                               domain::Snowflake channel_id) = 0;
    virtual void update_calendar_state(domain::CalendarId id, domain::CalendarState state) = 0;
    virtual void delete_calendar(domain::CalendarId id) = 0;
    virtual domain::Participant upsert_participant(const domain::Participant& participant) = 0;
    virtual std::optional<domain::Participant> find_participant(domain::CalendarId calendar_id,
                                                                 domain::Snowflake user_id) = 0;
    virtual int participant_count(domain::CalendarId calendar_id) = 0;
    virtual std::vector<domain::Participant> list_participants(domain::CalendarId calendar_id) = 0;
    virtual domain::IntervalId add_interval(const domain::AvailabilityInterval& interval) = 0;
    virtual std::vector<domain::AvailabilityInterval> list_intervals(
        domain::CalendarId calendar_id, domain::LocalDate first, domain::LocalDate last) = 0;
    virtual void remove_interval(domain::IntervalId id, domain::ParticipantId owner) = 0;
    virtual void clear_participant(domain::CalendarId calendar_id,
                                   domain::Snowflake user_id) = 0;
    virtual void set_calendar_message(domain::CalendarId calendar_id, domain::Snowflake guild_id,
                                      domain::Snowflake channel_id, domain::Snowflake message_id,
                                      int displayed_week, bool monthly) = 0;
    virtual std::optional<domain::CalendarMessageReference> find_calendar_message(
        domain::CalendarId calendar_id) = 0;
    virtual domain::EventId create_event(const domain::Event& event) = 0;
    virtual void set_attendance(const domain::AttendanceRecord& record) = 0;
};

} // namespace horelac::persistence
