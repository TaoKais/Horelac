#include "horelac/domain/ScheduleAlgorithms.hpp"
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <ranges>
using namespace horelac::domain;
TEST_CASE("leap February and partial weeks are correct") { auto weeks=month_weeks(std::chrono::year{2024}/2,WeekStart::monday);REQUIRE(weeks.size()==5);REQUIRE(static_cast<unsigned>(weeks.back().second.day())==29); }
TEST_CASE("one participant is not double counted") { Calendar c;c.id=7;c.config.month=std::chrono::year{2026}/8;c.config.timezone="Europe/Madrid";c.config.start_minute=18*60;c.config.end_minute=22*60;c.config.slot_minutes=30;const auto date=std::chrono::year{2026}/8/5;std::vector<AvailabilityInterval> intervals{{1,7,9,date,18*60,20*60},{2,7,9,date,19*60,21*60},{3,7,10,date,19*60+30,20*60+30}};auto map=aggregate_week(c,date,date,intervals,2);auto cell=std::ranges::find_if(map.cells,[](const auto& x){return x.start_minute==19*60+30;});REQUIRE(cell!=map.cells.end());REQUIRE(cell->available_count==2);}
TEST_CASE("best window prioritizes conservative minimum") {const auto date=std::chrono::year{2026}/8/5;WeeklyHeatmap h{date,date,30,10,{{date,1080,1110,10,10},{date,1110,1140,2,10},{date,1140,1170,8,10},{date,1170,1200,8,10},{date,1200,1230,8,10}}};auto result=rank_best_windows(h,60,1);REQUIRE(result.size()==1);REQUIRE(result[0].start_minute==1140);REQUIRE(result[0].minimum_available==8);}
TEST_CASE("duration must align to resolution") {WeeklyHeatmap h;h.slot_minutes=30;REQUIRE_THROWS(rank_best_windows(h,45));}
