#include "horelac/persistence/SQLiteScheduleRepository.hpp"
#include <catch2/catch_test_macros.hpp>
using namespace horelac;
TEST_CASE("SQLite migrations and cascade deletion work") {persistence::SQLiteScheduleRepository repo(":memory:");repo.migrate();domain::Calendar c;c.guild_id=1;c.channel_id=2;c.creator_user_id=3;c.title="Calendar";c.config.month=std::chrono::year{2026}/8;c.id=repo.create_calendar(c);auto p=repo.upsert_participant({0,c.id,4,domain::IdentityMode::anonymous});repo.add_interval({0,c.id,p.id,std::chrono::year{2026}/8/5,1080,1200});REQUIRE(repo.participant_count(c.id)==1);REQUIRE(repo.list_intervals(c.id,std::chrono::year{2026}/8/1,std::chrono::year{2026}/8/31).size()==1);repo.delete_calendar(c.id);REQUIRE_FALSE(repo.find_calendar(c.id).has_value());}

