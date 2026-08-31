#pragma once

#include "horelac/persistence/IScheduleRepository.hpp"

#include <mutex>
#include <string>

struct sqlite3;

namespace horelac::persistence {

class SQLiteScheduleRepository final : public IScheduleRepository {
  public:
    explicit SQLiteScheduleRepository(std::string path);
    ~SQLiteScheduleRepository() override;
    SQLiteScheduleRepository(const SQLiteScheduleRepository&) = delete;
    SQLiteScheduleRepository& operator=(const SQLiteScheduleRepository&) = delete;

    void migrate() override;
    domain::CalendarId create_calendar(const domain::Calendar& calendar) override;
    std::optional<domain::Calendar> find_calendar(domain::CalendarId id) override;
    std::vector<domain::Calendar> list_open_calendars(domain::Snowflake guild_id,
                                                       domain::Snowflake channel_id) override;
    void update_calendar_state(domain::CalendarId id, domain::CalendarState state) override;
    void delete_calendar(domain::CalendarId id) override;
    domain::Participant upsert_participant(const domain::Participant& participant) override;
    std::optional<domain::Participant> find_participant(domain::CalendarId calendar_id,
                                                         domain::Snowflake user_id) override;
    int participant_count(domain::CalendarId calendar_id) override;
    std::vector<domain::Participant> list_participants(domain::CalendarId calendar_id) override;
    domain::IntervalId add_interval(const domain::AvailabilityInterval& interval) override;
    std::vector<domain::AvailabilityInterval> list_intervals(domain::CalendarId calendar_id,
                                                              domain::LocalDate first,
                                                              domain::LocalDate last) override;
    void remove_interval(domain::IntervalId id, domain::ParticipantId owner) override;
    void clear_participant(domain::CalendarId calendar_id, domain::Snowflake user_id) override;
    void set_calendar_message(domain::CalendarId calendar_id, domain::Snowflake guild_id,
                              domain::Snowflake channel_id, domain::Snowflake message_id,
                              int displayed_week, bool monthly) override;
    std::optional<domain::CalendarMessageReference> find_calendar_message(
        domain::CalendarId calendar_id) override;
    domain::EventId create_event(const domain::Event& event) override;
    void set_attendance(const domain::AttendanceRecord& record) override;

  private:
    sqlite3* db_{};
    std::mutex mutex_;
    void execute(const char* sql);
};

} // namespace horelac::persistence
