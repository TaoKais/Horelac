#include "horelac/bot/DiscordBot.hpp"
#include "horelac/persistence/SQLiteScheduleRepository.hpp"
#include "horelac/services/ScheduleService.hpp"
#include "horelac/support/Config.hpp"
#include <exception>
#include <iostream>
int main(){try{auto config=horelac::support::Config::from_environment();horelac::persistence::SQLiteScheduleRepository repository(config.database_path);repository.migrate();horelac::services::ScheduleService service(repository);horelac::bot::DiscordBot bot(config,service);bot.run();return 0;}catch(const std::exception& e){std::cerr<<"Horelac startup failed: "<<e.what()<<'\n';return 1;}}

