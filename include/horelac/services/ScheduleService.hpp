#pragma once

#include "horelac/domain/ScheduleAlgorithms.hpp"
#include "horelac/persistence/IScheduleRepository.hpp"

#include <mutex>
#include <unordered_map>

namespace horelac::services {

struct CreateCalendarRequest {
    domain::Snowflake guild_id{};
    domain::Snowflake channel_id{};
    domain::Snowflake actor_user_id{};
    std::string title;
    std::string description;
    std::string locale{"en"};
    domain::CalendarConfig config;
};

struct AddAvailabilityRequest {
    domain::CalendarId calendar_id{};
    domain::Snowflake actor_user_id{};
    domain::IdentityMode identity_mode{domain::IdentityMode::anonymous};
    std::optional<std::string> alias;
    std::optional<std::string> display_name;
    std::vector<domain::LocalDate> dates;
    int start_minute{};
    int end_minute{};
};

class ScheduleService {
  public:
    explicit ScheduleService(persistence::IScheduleRepository& repository);
    domain::Calendar create_calendar(const CreateCalendarRequest& request);
    void add_availability(const AddAvailabilityRequest& request);
    domain::Calendar calendar(domain::CalendarId calendar_id);
    std::size_t week_count(domain::CalendarId calendar_id);
    std::vector<domain::AvailabilityInterval> my_schedule(domain::CalendarId calendar_id,
                                                           domain::Snowflake actor_user_id);
    std::vector<domain::Participant> day_participants(domain::CalendarId calendar_id,
                                                       domain::LocalDate date,
                                                       domain::Snowflake actor_user_id);
    void set_calendar_message(const domain::CalendarMessageReference& reference);
    std::optional<domain::CalendarMessageReference> calendar_message(
        domain::CalendarId calendar_id);
    void clear_my_data(domain::CalendarId calendar_id, domain::Snowflake actor_user_id);
    domain::WeeklyHeatmap weekly_heatmap(domain::CalendarId calendar_id, std::size_t week_index);
    std::vector<domain::BestTimeWindow> best_windows(domain::CalendarId calendar_id,
                                                     std::size_t week_index,
                                                     int duration_minutes,
                                                     std::size_t limit = 3);
    std::vector<std::string> public_participants(domain::CalendarId calendar_id);
    void change_state(domain::CalendarId calendar_id, domain::Snowflake actor_user_id,
                      domain::CalendarState state);
    void delete_calendar(domain::CalendarId calendar_id, domain::Snowflake actor_user_id);

  private:
    persistence::IScheduleRepository& repository_;
    std::mutex cache_mutex_;
    std::unordered_map<domain::CalendarId, domain::Calendar> calendar_cache_;
    domain::Calendar require_calendar(domain::CalendarId id);
    void invalidate(domain::CalendarId id);
};

} // namespace horelac::services
