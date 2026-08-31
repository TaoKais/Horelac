#include "horelac/domain/ScheduleAlgorithms.hpp"

#include "horelac/domain/Errors.hpp"

#include <algorithm>
#include <bitset>
#include <cmath>
#include <map>
#include <numeric>
#include <set>
#include <tuple>

namespace horelac::domain {
namespace {

constexpr int minutes_per_day = 24 * 60;

std::chrono::weekday configured_weekday(WeekStart start) {
    return start == WeekStart::monday ? std::chrono::Monday : std::chrono::Sunday;
}

} // namespace

bool is_valid_calendar_config(const CalendarConfig& c) noexcept {
    return c.month.ok() && !c.timezone.empty() && c.start_minute >= 0 &&
           c.end_minute <= minutes_per_day && c.start_minute < c.end_minute &&
           (c.slot_minutes == 15 || c.slot_minutes == 30 || c.slot_minutes == 60) &&
           (c.end_minute - c.start_minute) % c.slot_minutes == 0;
}

bool is_date_in_month(LocalDate date, std::chrono::year_month month) noexcept {
    return date.ok() && date.year() == month.year() && date.month() == month.month();
}

std::vector<std::pair<LocalDate, LocalDate>> month_weeks(std::chrono::year_month month,
                                                         WeekStart start) {
    if (!month.ok()) {
        throw DomainError(ErrorCode::invalid_input, "Invalid month");
    }
    const LocalDate first{month / std::chrono::day{1}};
    const LocalDate last{month / std::chrono::last};
    const auto first_day = std::chrono::sys_days{first};
    const auto last_day = std::chrono::sys_days{last};
    const auto week_start = configured_weekday(start);
    const auto offset = (std::chrono::weekday{first_day} - week_start).count();
    auto cursor = first_day - std::chrono::days{offset};
    std::vector<std::pair<LocalDate, LocalDate>> result;
    while (cursor <= last_day) {
        const auto clipped_first = std::max(cursor, first_day);
        const auto clipped_last = std::min(cursor + std::chrono::days{6}, last_day);
        result.emplace_back(LocalDate{clipped_first}, LocalDate{clipped_last});
        cursor += std::chrono::days{7};
    }
    return result;
}

WeeklyHeatmap aggregate_week(const Calendar& calendar, LocalDate first, LocalDate last,
                             std::span<const AvailabilityInterval> intervals,
                             int total_participants) {
    if (!is_valid_calendar_config(calendar.config) || first > last || total_participants < 0) {
        throw DomainError(ErrorCode::invalid_input, "Invalid heatmap input");
    }
    const int slots =
        (calendar.config.end_minute - calendar.config.start_minute) / calendar.config.slot_minutes;
    WeeklyHeatmap result{first, last, calendar.config.slot_minutes, total_participants, {}};
    std::map<LocalDate, std::vector<int>> counts;
    for (auto day = std::chrono::sys_days{first}; day <= std::chrono::sys_days{last};
         day += std::chrono::days{1}) {
        counts[LocalDate{day}] = std::vector<int>(static_cast<std::size_t>(slots), 0);
    }

    std::map<std::pair<ParticipantId, LocalDate>, std::vector<bool>> coverage;
    for (const auto& interval : intervals) {
        if (interval.calendar_id != calendar.id || interval.date < first || interval.date > last) {
            continue;
        }
        auto& bits = coverage[{interval.participant_id, interval.date}];
        if (bits.empty()) {
            bits.resize(static_cast<std::size_t>(slots));
        }
        const int clipped_start = std::max(interval.start_minute, calendar.config.start_minute);
        const int clipped_end = std::min(interval.end_minute, calendar.config.end_minute);
        for (int i = 0; i < slots; ++i) {
            const int slot_start = calendar.config.start_minute + i * calendar.config.slot_minutes;
            const int slot_end = slot_start + calendar.config.slot_minutes;
            if (clipped_start < slot_end && clipped_end > slot_start) {
                bits[static_cast<std::size_t>(i)] = true;
            }
        }
    }
    for (const auto& [key, bits] : coverage) {
        auto& day_counts = counts.at(key.second);
        for (std::size_t i = 0; i < bits.size(); ++i) {
            day_counts[i] += bits[i] ? 1 : 0;
        }
    }
    for (const auto& [date, day_counts] : counts) {
        for (int i = 0; i < slots; ++i) {
            const int start = calendar.config.start_minute + i * calendar.config.slot_minutes;
            result.cells.push_back({date, start, start + calendar.config.slot_minutes,
                                    day_counts[static_cast<std::size_t>(i)], total_participants});
        }
    }
    return result;
}

std::vector<BestTimeWindow> rank_best_windows(const WeeklyHeatmap& heatmap,
                                               int duration_minutes, std::size_t limit,
                                               std::optional<int> minimum_count,
                                               std::optional<double> minimum_percentage) {
    if (duration_minutes <= 0 || heatmap.slot_minutes <= 0 ||
        duration_minutes % heatmap.slot_minutes != 0) {
        throw DomainError(ErrorCode::invalid_input, "Duration must align to slot resolution");
    }
    const std::size_t needed = static_cast<std::size_t>(duration_minutes / heatmap.slot_minutes);
    std::map<LocalDate, std::vector<const HeatmapCell*>> by_day;
    for (const auto& cell : heatmap.cells) {
        by_day[cell.date].push_back(&cell);
    }
    std::vector<BestTimeWindow> candidates;
    for (auto& [date, cells] : by_day) {
        std::ranges::sort(cells, {}, [](const HeatmapCell* c) { return c->start_minute; });
        for (std::size_t i = 0; i + needed <= cells.size(); ++i) {
            bool consecutive = true;
            std::vector<double> values;
            values.reserve(needed);
            for (std::size_t j = 0; j < needed; ++j) {
                if (j > 0 && cells[i + j - 1]->end_minute != cells[i + j]->start_minute) {
                    consecutive = false;
                }
                values.push_back(static_cast<double>(cells[i + j]->available_count));
            }
            if (!consecutive) {
                continue;
            }
            const auto minimum = static_cast<int>(*std::ranges::min_element(values));
            const double average = std::accumulate(values.begin(), values.end(), 0.0) /
                                   static_cast<double>(values.size());
            double variance = 0.0;
            for (double value : values) {
                variance += (value - average) * (value - average);
            }
            variance /= static_cast<double>(values.size());
            const double percent = heatmap.total_participants == 0
                                       ? 0.0
                                       : 100.0 * minimum / heatmap.total_participants;
            if ((minimum_count && minimum < *minimum_count) ||
                (minimum_percentage && percent < *minimum_percentage)) {
                continue;
            }
            candidates.push_back({date, cells[i]->start_minute, cells[i + needed - 1]->end_minute,
                                  minimum, average, variance, heatmap.total_participants});
        }
    }
    std::ranges::sort(candidates, [](const auto& a, const auto& b) {
        return std::tuple{-a.minimum_available, -a.average_available, a.variance, a.date,
                          a.start_minute} <
               std::tuple{-b.minimum_available, -b.average_available, b.variance, b.date,
                          b.start_minute};
    });
    std::vector<BestTimeWindow> selected;
    for (const auto& candidate : candidates) {
        const bool too_similar = std::ranges::any_of(selected, [&](const auto& chosen) {
            if (chosen.date != candidate.date) {
                return false;
            }
            const int overlap = std::max(0, std::min(chosen.end_minute, candidate.end_minute) -
                                                std::max(chosen.start_minute, candidate.start_minute));
            return overlap * 2 >= duration_minutes;
        });
        if (!too_similar) {
            selected.push_back(candidate);
            if (selected.size() == limit) {
                break;
            }
        }
    }
    return selected;
}

} // namespace horelac::domain
