#pragma once

#include "horelac/domain/Models.hpp"

#include <optional>
#include <span>
#include <vector>

namespace horelac::domain {

[[nodiscard]] bool is_valid_calendar_config(const CalendarConfig& config) noexcept;
[[nodiscard]] bool is_date_in_month(LocalDate date, std::chrono::year_month month) noexcept;
[[nodiscard]] std::vector<std::pair<LocalDate, LocalDate>> month_weeks(
    std::chrono::year_month month, WeekStart start);
[[nodiscard]] WeeklyHeatmap aggregate_week(const Calendar& calendar, LocalDate first,
                                           LocalDate last,
                                           std::span<const AvailabilityInterval> intervals,
                                           int total_participants);
[[nodiscard]] std::vector<BestTimeWindow> rank_best_windows(
    const WeeklyHeatmap& heatmap, int duration_minutes, std::size_t limit = 3,
    std::optional<int> minimum_count = std::nullopt,
    std::optional<double> minimum_percentage = std::nullopt);

} // namespace horelac::domain

