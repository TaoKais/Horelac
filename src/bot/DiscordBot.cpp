#include "horelac/bot/DiscordBot.hpp"
#include "horelac/domain/Errors.hpp"
#include <charconv>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <format>
#include <iostream>
#include <variant>
namespace horelac::bot {
namespace {
std::int64_t integer(const dpp::slashcommand_t& e,const char* name,std::int64_t fallback=0){const auto v=e.get_parameter(name);return std::holds_alternative<std::int64_t>(v)?std::get<std::int64_t>(v):fallback;}
std::string string(const dpp::slashcommand_t& e,const char* name,std::string fallback={}){const auto v=e.get_parameter(name);return std::holds_alternative<std::string>(v)?std::get<std::string>(v):std::move(fallback);}
std::optional<std::int64_t> parse_id(std::string_view id,std::string_view action){const auto prefix=std::string("hsb:v1:")+std::string(action)+":";if(!id.starts_with(prefix))return std::nullopt;std::int64_t result{};const auto raw=id.substr(prefix.size());const auto [p,ec]=std::from_chars(raw.data(),raw.data()+raw.size(),result);return ec==std::errc{}&&p==raw.data()+raw.size()?std::optional{result}:std::nullopt;}
dpp::component button(std::string label,std::string id,dpp::component_style style=dpp::cos_primary){return dpp::component().set_type(dpp::cot_button).set_style(style).set_label(std::move(label)).set_id(std::move(id));}
}
DiscordBot::DiscordBot(const support::Config& config,services::ScheduleService& service):config_(config),schedules_(service),localization_(config.default_locale),cluster_(config.discord_token,dpp::i_default_intents) {register_handlers();}
void DiscordBot::run(){cluster_.start(dpp::st_wait);}
void DiscordBot::register_handlers(){
    cluster_.on_log([](const dpp::log_t& event){if(event.severity>=dpp::ll_info)std::cerr<<event.message<<'\n';});
    cluster_.on_ready([this](const dpp::ready_t& event){if(dpp::run_once<struct register_bot_commands>()){register_commands();}std::cerr<<"Horelac connected as "<<cluster_.me.username<<'\n';});
    cluster_.on_slashcommand([this](const dpp::slashcommand_t& event){if(event.command.get_command_name()=="schedule")handle_schedule(event);});
    cluster_.on_button_click([this](const dpp::button_click_t& event){handle_button(event);});
}
void DiscordBot::register_commands(){
    dpp::slashcommand command("schedule","Create and manage availability calendars",config_.application_id);
    command.add_option(dpp::command_option(dpp::co_sub_command,"create","Create a calendar")
        .add_option(dpp::command_option(dpp::co_string,"name","Calendar name",true).set_max_length(100))
        .add_option(dpp::command_option(dpp::co_string,"month","Month as YYYY-MM",true).set_max_length(7))
        .add_option(dpp::command_option(dpp::co_string,"timezone","IANA timezone",true).set_max_length(64))
        .add_option(dpp::command_option(dpp::co_integer,"start","Start hour (0-23)",true).set_min_value(0).set_max_value(23))
        .add_option(dpp::command_option(dpp::co_integer,"end","End hour (1-24)",true).set_min_value(1).set_max_value(24)));
    command.add_option(dpp::command_option(dpp::co_sub_command,"add","Add availability")
        .add_option(dpp::command_option(dpp::co_integer,"calendar","Calendar ID",true))
        .add_option(dpp::command_option(dpp::co_string,"date","Date as YYYY-MM-DD",true).set_max_length(10))
        .add_option(dpp::command_option(dpp::co_integer,"start","Start minutes after midnight",true).set_min_value(0).set_max_value(1439))
        .add_option(dpp::command_option(dpp::co_integer,"end","End minutes after midnight",true).set_min_value(1).set_max_value(1440))
        .add_option(dpp::command_option(dpp::co_string,"alias","Optional calendar alias",false).set_max_length(32)));
    command.add_option(dpp::command_option(dpp::co_sub_command,"view","View a calendar week")
        .add_option(dpp::command_option(dpp::co_integer,"calendar","Calendar ID",true))
        .add_option(dpp::command_option(dpp::co_integer,"week","Week number",false).set_min_value(1).set_max_value(6)));
    command.add_option(dpp::command_option(dpp::co_sub_command,"best","Find best continuous windows")
        .add_option(dpp::command_option(dpp::co_integer,"calendar","Calendar ID",true))
        .add_option(dpp::command_option(dpp::co_integer,"duration","Duration in minutes",true).set_min_value(15).set_max_value(720))
        .add_option(dpp::command_option(dpp::co_integer,"week","Week number",false).set_min_value(1).set_max_value(6)));
    command.add_option(dpp::command_option(dpp::co_sub_command,"clear","Delete your data in a calendar").add_option(dpp::command_option(dpp::co_integer,"calendar","Calendar ID",true)));
    if(config_.development_guild_id)cluster_.guild_command_create(command,*config_.development_guild_id);else cluster_.global_command_create(command);
}
void DiscordBot::handle_schedule(const dpp::slashcommand_t& event){
    try{const auto sub=event.command.get_command_interaction().options.at(0).name;const auto guild=static_cast<domain::Snowflake>(event.command.guild_id);const auto channel=static_cast<domain::Snowflake>(event.command.channel_id);const auto user=static_cast<domain::Snowflake>(event.command.get_issuing_user().id);
        if(sub=="create"){const auto month=string(event,"month");int y{},m{};if(std::sscanf(month.c_str(),"%d-%d",&y,&m)!=2)throw domain::DomainError(domain::ErrorCode::invalid_input,"Month must be YYYY-MM");domain::CalendarConfig cfg;cfg.month=std::chrono::year{y}/std::chrono::month{static_cast<unsigned>(m)};cfg.timezone=string(event,"timezone");cfg.start_minute=static_cast<int>(integer(event,"start"))*60;cfg.end_minute=static_cast<int>(integer(event,"end"))*60;auto calendar=schedules_.create_calendar({guild,channel,user,string(event,"name"),"",config_.default_locale,cfg});dpp::message message(std::format("**{}**\nCalendar `{}` · {} · {}",calendar.title,calendar.id,month,cfg.timezone));message.add_component(dpp::component().add_component(button("Add Availability",std::format("hsb:v1:add:{}",calendar.id))).add_component(button("View Calendar",std::format("hsb:v1:view:{}",calendar.id))).add_component(button("My Schedule",std::format("hsb:v1:mine:{}",calendar.id),dpp::cos_secondary)).add_component(button("Participants",std::format("hsb:v1:participants:{}",calendar.id),dpp::cos_secondary)));event.reply(message);}
        else if(sub=="add"){const auto cid=integer(event,"calendar");const auto date=string(event,"date");int y{},m{},d{};if(std::sscanf(date.c_str(),"%d-%d-%d",&y,&m,&d)!=3)throw domain::DomainError(domain::ErrorCode::invalid_input,"Date must be YYYY-MM-DD");const auto alias=string(event,"alias");schedules_.add_availability({cid,user,alias.empty()?domain::IdentityMode::anonymous:domain::IdentityMode::alias,alias.empty()?std::nullopt:std::optional{alias},event.command.get_issuing_user().global_name,{std::chrono::year{y}/std::chrono::month{static_cast<unsigned>(m)}/std::chrono::day{static_cast<unsigned>(d)}},static_cast<int>(integer(event,"start")),static_cast<int>(integer(event,"end"))});event.reply(dpp::message(localization_.text(config_.default_locale,"saved")).set_flags(dpp::m_ephemeral));}
        else if(sub=="view"){const auto cid=integer(event,"calendar");const auto week=static_cast<std::size_t>(std::max<std::int64_t>(1,integer(event,"week",1))-1);const auto map=schedules_.weekly_heatmap(cid,week);int peak=0;for(const auto& c:map.cells)peak=std::max(peak,c.available_count);event.reply(std::format("Calendar `{}` · week {} · {} participants · peak overlap {}",cid,week+1,map.total_participants,peak));}
        else if(sub=="best"){const auto cid=integer(event,"calendar");const auto week=static_cast<std::size_t>(std::max<std::int64_t>(1,integer(event,"week",1))-1);const auto windows=schedules_.best_windows(cid,week,static_cast<int>(integer(event,"duration")));std::string response="**Best windows**\n";for(const auto& w:windows)response+=std::format("{:04}-{:02}-{:02} {:02}:{:02}-{:02}:{:02} — {}/{} minimum\n",static_cast<int>(w.date.year()),static_cast<unsigned>(w.date.month()),static_cast<unsigned>(w.date.day()),w.start_minute/60,w.start_minute%60,w.end_minute/60,w.end_minute%60,w.minimum_available,w.total_participants);event.reply(response);}
        else if(sub=="clear"){schedules_.clear_my_data(integer(event,"calendar"),user);event.reply(dpp::message("Your scheduling data was removed.").set_flags(dpp::m_ephemeral));}
    }catch(const std::exception& error){event.reply(dpp::message(friendly_error(error)).set_flags(dpp::m_ephemeral));}}
void DiscordBot::handle_button(const dpp::button_click_t& event){const auto id=event.custom_id;if(auto calendar=parse_id(id,"view")){try{const auto map=schedules_.weekly_heatmap(*calendar,0);event.reply(dpp::message(std::format("Week 1: {} participants",map.total_participants)).set_flags(dpp::m_ephemeral));}catch(const std::exception& e){event.reply(dpp::message(friendly_error(e)).set_flags(dpp::m_ephemeral));}}else if(parse_id(id,"add")){event.reply(dpp::message("Use `/schedule add` to submit an interval. Interactive modal entry is enabled in the next UI revision.").set_flags(dpp::m_ephemeral));}else if(auto calendar=parse_id(id,"participants")){try{const auto names=schedules_.public_participants(*calendar);std::string out=names.empty()?"Participant identities are private.":"Participants:\n";for(const auto& name:names)out+="• "+name+"\n";event.reply(dpp::message(out).set_flags(dpp::m_ephemeral));}catch(const std::exception& e){event.reply(dpp::message(friendly_error(e)).set_flags(dpp::m_ephemeral));}}else event.reply(dpp::message("This control is unavailable or invalid.").set_flags(dpp::m_ephemeral));}
std::string DiscordBot::friendly_error(const std::exception& error){if(const auto* domain_error=dynamic_cast<const domain::DomainError*>(&error)){switch(domain_error->code()){case domain::ErrorCode::calendar_not_found:return "Calendar not found.";case domain::ErrorCode::permission_denied:return "You are not allowed to do that.";case domain::ErrorCode::calendar_closed:return "This calendar is closed.";case domain::ErrorCode::alias_already_used:return "That alias is already in use.";default:return domain_error->what();}}return "The request could not be completed.";}
} // namespace horelac::bot
