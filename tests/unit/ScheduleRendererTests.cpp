#include "horelac/render/ScheduleRenderer.hpp"
#include "horelac/domain/ScheduleAlgorithms.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>
using namespace horelac;

TEST_CASE("renderer produces an anonymous PNG after availability changes") {
    domain::Calendar calendar;
    calendar.id=7;calendar.title="Anonymous September";calendar.config.month=std::chrono::year{2026}/9;
    calendar.config.start_minute=18*60;calendar.config.end_minute=20*60;calendar.config.slot_minutes=30;
    const auto date=std::chrono::year{2026}/9/5;
    const std::vector<domain::AvailabilityInterval> intervals{{1,7,9,date,18*60,19*60},{2,7,9,date,18*60+30,19*60+30}};
    const auto heatmap=domain::aggregate_week(calendar,date,date,intervals,1);
    render::CairoScheduleRenderer renderer;
    const auto result=renderer.render_weekly(calendar,heatmap,0,5);
    REQUIRE(result.png.size()>8);
    REQUIRE(result.png[0]==0x89);
    REQUIRE(result.png[1]=='P');
    const std::string bytes(reinterpret_cast<const char*>(result.png.data()),result.png.size());
    REQUIRE(bytes.find("discord-user-9")==std::string::npos);
}
