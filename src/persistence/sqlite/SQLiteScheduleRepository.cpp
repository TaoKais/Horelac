#include "horelac/persistence/SQLiteScheduleRepository.hpp"

#include "horelac/domain/Errors.hpp"

#include <sqlite3.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <memory>
#include <stdexcept>

namespace horelac::persistence {
namespace {

using Statement = std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)>;

[[noreturn]] void fail(sqlite3* db, const char* context) {
    throw domain::DomainError(domain::ErrorCode::database_error,
                              std::string(context) + ": " + sqlite3_errmsg(db));
}

Statement prepare(sqlite3* db, const char* sql) {
    sqlite3_stmt* raw{};
    if (sqlite3_prepare_v2(db, sql, -1, &raw, nullptr) != SQLITE_OK) {
        fail(db, "prepare");
    }
    return Statement(raw, sqlite3_finalize);
}

void done(sqlite3* db, sqlite3_stmt* statement) {
    if (sqlite3_step(statement) != SQLITE_DONE) {
        fail(db, "execute");
    }
}

void bind_text(sqlite3* db, sqlite3_stmt* stmt, int index, const std::string& value) {
    if (sqlite3_bind_text(stmt, index, value.c_str(), static_cast<int>(value.size()),
                          SQLITE_TRANSIENT) != SQLITE_OK) {
        fail(db, "bind text");
    }
}

std::string date_string(domain::LocalDate date) {
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(date.year()),
                       static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
}

domain::LocalDate parse_date(const unsigned char* raw) {
    int y{}, m{}, d{};
    const std::string text{reinterpret_cast<const char*>(raw)};
    if (std::sscanf(text.c_str(), "%d-%d-%d", &y, &m, &d) != 3) {
        throw domain::DomainError(domain::ErrorCode::database_error, "Invalid stored date");
    }
    return std::chrono::year{y} / std::chrono::month{static_cast<unsigned>(m)} /
           std::chrono::day{static_cast<unsigned>(d)};
}

domain::Calendar read_calendar(sqlite3_stmt* s) {
    domain::Calendar c;
    c.id = sqlite3_column_int64(s, 0);
    c.guild_id = static_cast<domain::Snowflake>(sqlite3_column_int64(s, 1));
    c.channel_id = static_cast<domain::Snowflake>(sqlite3_column_int64(s, 2));
    c.creator_user_id = static_cast<domain::Snowflake>(sqlite3_column_int64(s, 3));
    c.title = reinterpret_cast<const char*>(sqlite3_column_text(s, 4));
    c.description = reinterpret_cast<const char*>(sqlite3_column_text(s, 5));
    const auto year = std::chrono::year{sqlite3_column_int(s, 6)};
    const auto month = std::chrono::month{static_cast<unsigned>(sqlite3_column_int(s, 7))};
    c.config.month = year / month;
    c.config.timezone = reinterpret_cast<const char*>(sqlite3_column_text(s, 8));
    c.config.start_minute = sqlite3_column_int(s, 9);
    c.config.end_minute = sqlite3_column_int(s, 10);
    c.config.slot_minutes = sqlite3_column_int(s, 11);
    c.config.week_start = static_cast<domain::WeekStart>(sqlite3_column_int(s, 12));
    c.config.allowed_identity_modes = static_cast<std::uint8_t>(sqlite3_column_int(s, 13));
    c.config.default_identity_mode = static_cast<domain::IdentityMode>(sqlite3_column_int(s, 14));
    c.config.cell_value_mode = static_cast<domain::CellValueMode>(sqlite3_column_int(s, 15));
    c.state = static_cast<domain::CalendarState>(sqlite3_column_int(s, 16));
    c.locale = reinterpret_cast<const char*>(sqlite3_column_text(s, 17));
    c.revision = sqlite3_column_int64(s, 18);
    return c;
}

constexpr auto calendar_columns =
    "id,guild_id,channel_id,creator_user_id,title,description,year,month,timezone,"
    "start_minute,end_minute,slot_minutes,week_start,identity_modes,default_identity_mode,"
    "cell_value_mode,state,locale,revision";

} // namespace

SQLiteScheduleRepository::SQLiteScheduleRepository(std::string path) {
    if (path != ":memory:") {
        const auto parent = std::filesystem::path(path).parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent);
        }
    }
    if (sqlite3_open_v2(path.c_str(), &db_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE |
                                               SQLITE_OPEN_FULLMUTEX,
                        nullptr) != SQLITE_OK) {
        fail(db_, "open database");
    }
    execute("PRAGMA foreign_keys=ON");
    execute("PRAGMA busy_timeout=5000");
    execute("PRAGMA journal_mode=WAL");
}

SQLiteScheduleRepository::~SQLiteScheduleRepository() {
    if (db_) {
        sqlite3_close_v2(db_);
    }
}

void SQLiteScheduleRepository::execute(const char* sql) {
    char* error{};
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &error) != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(db_);
        sqlite3_free(error);
        throw domain::DomainError(domain::ErrorCode::database_error, message);
    }
}

void SQLiteScheduleRepository::migrate() {
    std::scoped_lock lock(mutex_);
    execute("BEGIN IMMEDIATE");
    try {
        execute("CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
        auto check = prepare(db_, "SELECT 1 FROM schema_migrations WHERE version=1");
        if (sqlite3_step(check.get()) != SQLITE_ROW) {
            // Kept in code so installed binaries do not depend on the working directory.
            execute(R"sql(
CREATE TABLE guilds(guild_id INTEGER PRIMARY KEY,locale TEXT NOT NULL DEFAULT 'en',created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE channels(channel_id INTEGER PRIMARY KEY,guild_id INTEGER NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE calendars(id INTEGER PRIMARY KEY AUTOINCREMENT,guild_id INTEGER NOT NULL,channel_id INTEGER NOT NULL,creator_user_id INTEGER NOT NULL,title TEXT NOT NULL CHECK(length(title) BETWEEN 1 AND 100),description TEXT NOT NULL DEFAULT '' CHECK(length(description)<=1000),year INTEGER NOT NULL,month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12),timezone TEXT NOT NULL,start_minute INTEGER NOT NULL,end_minute INTEGER NOT NULL,slot_minutes INTEGER NOT NULL,week_start INTEGER NOT NULL,identity_modes INTEGER NOT NULL,default_identity_mode INTEGER NOT NULL,cell_value_mode INTEGER NOT NULL DEFAULT 0,state INTEGER NOT NULL DEFAULT 0,locale TEXT NOT NULL DEFAULT 'en',revision INTEGER NOT NULL DEFAULT 1,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE calendar_moderators(calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,user_id INTEGER NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(calendar_id,user_id));
CREATE TABLE participants(id INTEGER PRIMARY KEY AUTOINCREMENT,calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,discord_user_id INTEGER NOT NULL,identity_mode INTEGER NOT NULL,alias TEXT,alias_key TEXT,display_name TEXT,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,UNIQUE(calendar_id,discord_user_id),UNIQUE(calendar_id,alias_key));
CREATE TABLE availability_intervals(id INTEGER PRIMARY KEY AUTOINCREMENT,calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,participant_id INTEGER NOT NULL REFERENCES participants(id) ON DELETE CASCADE,local_date TEXT NOT NULL,start_minute INTEGER NOT NULL,end_minute INTEGER NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,CHECK(start_minute>=0 AND end_minute<=1440 AND start_minute<end_minute));
CREATE TABLE calendar_messages(calendar_id INTEGER PRIMARY KEY REFERENCES calendars(id) ON DELETE CASCADE,guild_id INTEGER NOT NULL,channel_id INTEGER NOT NULL,message_id INTEGER NOT NULL,displayed_week INTEGER NOT NULL DEFAULT 0,view_mode INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE events(id INTEGER PRIMARY KEY AUTOINCREMENT,source_calendar_id INTEGER REFERENCES calendars(id) ON DELETE SET NULL,guild_id INTEGER NOT NULL,channel_id INTEGER NOT NULL,creator_user_id INTEGER NOT NULL,title TEXT NOT NULL,description TEXT NOT NULL DEFAULT '',starts_at_utc INTEGER NOT NULL,timezone TEXT NOT NULL,duration_minutes INTEGER NOT NULL CHECK(duration_minutes>0),state INTEGER NOT NULL DEFAULT 0,message_id INTEGER,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE TABLE event_attendance(event_id INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,discord_user_id INTEGER NOT NULL,response INTEGER NOT NULL,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,PRIMARY KEY(event_id,discord_user_id));
CREATE TABLE reminder_jobs(id INTEGER PRIMARY KEY AUTOINCREMENT,event_id INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,due_at_utc INTEGER NOT NULL,kind INTEGER NOT NULL,state INTEGER NOT NULL DEFAULT 0,attempts INTEGER NOT NULL DEFAULT 0,updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);
CREATE INDEX idx_calendars_guild_channel_state ON calendars(guild_id,channel_id,state);
CREATE INDEX idx_intervals_calendar_date ON availability_intervals(calendar_id,local_date,start_minute,end_minute);
CREATE INDEX idx_intervals_participant_date ON availability_intervals(participant_id,local_date);
CREATE INDEX idx_calendar_messages_message ON calendar_messages(message_id);
CREATE INDEX idx_events_guild_start ON events(guild_id,starts_at_utc);
CREATE INDEX idx_reminders_due ON reminder_jobs(state,due_at_utc);
)sql");
            execute("INSERT INTO schema_migrations(version,name) VALUES(1,'initial')");
        }
        execute("COMMIT");
    } catch (...) {
        execute("ROLLBACK");
        throw;
    }
}

domain::CalendarId SQLiteScheduleRepository::create_calendar(const domain::Calendar& c) {
    std::scoped_lock lock(mutex_);
    auto guild = prepare(db_, "INSERT OR IGNORE INTO guilds(guild_id,locale) VALUES(?,?)");
    sqlite3_bind_int64(guild.get(), 1, static_cast<sqlite3_int64>(c.guild_id));
    bind_text(db_, guild.get(), 2, c.locale);
    done(db_, guild.get());
    auto channel = prepare(db_, "INSERT OR IGNORE INTO channels(channel_id,guild_id) VALUES(?,?)");
    sqlite3_bind_int64(channel.get(), 1, static_cast<sqlite3_int64>(c.channel_id));
    sqlite3_bind_int64(channel.get(), 2, static_cast<sqlite3_int64>(c.guild_id));
    done(db_, channel.get());
    auto s = prepare(db_, "INSERT INTO calendars(guild_id,channel_id,creator_user_id,title,description,year,month,timezone,start_minute,end_minute,slot_minutes,week_start,identity_modes,default_identity_mode,cell_value_mode,state,locale) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
    sqlite3_bind_int64(s.get(), 1, static_cast<sqlite3_int64>(c.guild_id));
    sqlite3_bind_int64(s.get(), 2, static_cast<sqlite3_int64>(c.channel_id));
    sqlite3_bind_int64(s.get(), 3, static_cast<sqlite3_int64>(c.creator_user_id));
    bind_text(db_, s.get(), 4, c.title); bind_text(db_, s.get(), 5, c.description);
    sqlite3_bind_int(s.get(), 6, static_cast<int>(c.config.month.year()));
    sqlite3_bind_int(s.get(), 7, static_cast<int>(static_cast<unsigned>(c.config.month.month())));
    bind_text(db_, s.get(), 8, c.config.timezone);
    sqlite3_bind_int(s.get(), 9, c.config.start_minute); sqlite3_bind_int(s.get(), 10, c.config.end_minute);
    sqlite3_bind_int(s.get(), 11, c.config.slot_minutes); sqlite3_bind_int(s.get(), 12, static_cast<int>(c.config.week_start));
    sqlite3_bind_int(s.get(), 13, c.config.allowed_identity_modes); sqlite3_bind_int(s.get(), 14, static_cast<int>(c.config.default_identity_mode));
    sqlite3_bind_int(s.get(), 15, static_cast<int>(c.config.cell_value_mode)); sqlite3_bind_int(s.get(), 16, static_cast<int>(c.state));
    bind_text(db_, s.get(), 17, c.locale); done(db_, s.get());
    return sqlite3_last_insert_rowid(db_);
}

std::optional<domain::Calendar> SQLiteScheduleRepository::find_calendar(domain::CalendarId id) {
    std::scoped_lock lock(mutex_);
    const auto sql = std::string("SELECT ") + calendar_columns + " FROM calendars WHERE id=?";
    auto s = prepare(db_, sql.c_str()); sqlite3_bind_int64(s.get(), 1, id);
    if (sqlite3_step(s.get()) == SQLITE_ROW) return read_calendar(s.get());
    return std::nullopt;
}

std::vector<domain::Calendar> SQLiteScheduleRepository::list_open_calendars(domain::Snowflake guild, domain::Snowflake channel) {
    std::scoped_lock lock(mutex_); std::vector<domain::Calendar> out;
    const auto sql = std::string("SELECT ") + calendar_columns + " FROM calendars WHERE guild_id=? AND channel_id=? AND state=0 ORDER BY id DESC";
    auto s=prepare(db_,sql.c_str()); sqlite3_bind_int64(s.get(),1,static_cast<sqlite3_int64>(guild)); sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(channel));
    while(sqlite3_step(s.get())==SQLITE_ROW) out.push_back(read_calendar(s.get())); return out;
}

void SQLiteScheduleRepository::update_calendar_state(domain::CalendarId id, domain::CalendarState state) { std::scoped_lock lock(mutex_); auto s=prepare(db_,"UPDATE calendars SET state=?,revision=revision+1,updated_at=CURRENT_TIMESTAMP WHERE id=?"); sqlite3_bind_int(s.get(),1,static_cast<int>(state)); sqlite3_bind_int64(s.get(),2,id); done(db_,s.get()); }
void SQLiteScheduleRepository::delete_calendar(domain::CalendarId id) { std::scoped_lock lock(mutex_); auto s=prepare(db_,"DELETE FROM calendars WHERE id=?"); sqlite3_bind_int64(s.get(),1,id); done(db_,s.get()); }

domain::Participant SQLiteScheduleRepository::upsert_participant(const domain::Participant& p) {
    std::scoped_lock lock(mutex_); auto s=prepare(db_,"INSERT INTO participants(calendar_id,discord_user_id,identity_mode,alias,alias_key,display_name) VALUES(?,?,?,?,lower(?),?) ON CONFLICT(calendar_id,discord_user_id) DO UPDATE SET identity_mode=excluded.identity_mode,alias=excluded.alias,alias_key=excluded.alias_key,display_name=excluded.display_name,updated_at=CURRENT_TIMESTAMP RETURNING id");
    sqlite3_bind_int64(s.get(),1,p.calendar_id); sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(p.discord_user_id)); sqlite3_bind_int(s.get(),3,static_cast<int>(p.identity_mode));
    if(p.alias){bind_text(db_,s.get(),4,*p.alias);bind_text(db_,s.get(),5,*p.alias);}else{sqlite3_bind_null(s.get(),4);sqlite3_bind_null(s.get(),5);} if(p.display_name)bind_text(db_,s.get(),6,*p.display_name);else sqlite3_bind_null(s.get(),6);
    if(sqlite3_step(s.get())!=SQLITE_ROW) fail(db_,"upsert participant"); auto out=p; out.id=sqlite3_column_int64(s.get(),0); return out;
}

std::optional<domain::Participant> SQLiteScheduleRepository::find_participant(domain::CalendarId cid, domain::Snowflake uid) { std::scoped_lock lock(mutex_); auto s=prepare(db_,"SELECT id,identity_mode,alias,display_name FROM participants WHERE calendar_id=? AND discord_user_id=?");sqlite3_bind_int64(s.get(),1,cid);sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(uid));if(sqlite3_step(s.get())!=SQLITE_ROW)return std::nullopt;domain::Participant p{sqlite3_column_int64(s.get(),0),cid,uid,static_cast<domain::IdentityMode>(sqlite3_column_int(s.get(),1))};if(sqlite3_column_type(s.get(),2)!=SQLITE_NULL)p.alias=reinterpret_cast<const char*>(sqlite3_column_text(s.get(),2));if(sqlite3_column_type(s.get(),3)!=SQLITE_NULL)p.display_name=reinterpret_cast<const char*>(sqlite3_column_text(s.get(),3));return p; }
int SQLiteScheduleRepository::participant_count(domain::CalendarId cid){std::scoped_lock lock(mutex_);auto s=prepare(db_,"SELECT count(*) FROM participants WHERE calendar_id=?");sqlite3_bind_int64(s.get(),1,cid);return sqlite3_step(s.get())==SQLITE_ROW?sqlite3_column_int(s.get(),0):0;}
std::vector<domain::Participant> SQLiteScheduleRepository::list_participants(domain::CalendarId cid){std::scoped_lock lock(mutex_);std::vector<domain::Participant> out;auto s=prepare(db_,"SELECT id,discord_user_id,identity_mode,alias,display_name FROM participants WHERE calendar_id=? ORDER BY coalesce(alias,display_name,'')");sqlite3_bind_int64(s.get(),1,cid);while(sqlite3_step(s.get())==SQLITE_ROW){domain::Participant p{sqlite3_column_int64(s.get(),0),cid,static_cast<domain::Snowflake>(sqlite3_column_int64(s.get(),1)),static_cast<domain::IdentityMode>(sqlite3_column_int(s.get(),2))};if(sqlite3_column_type(s.get(),3)!=SQLITE_NULL)p.alias=reinterpret_cast<const char*>(sqlite3_column_text(s.get(),3));if(sqlite3_column_type(s.get(),4)!=SQLITE_NULL)p.display_name=reinterpret_cast<const char*>(sqlite3_column_text(s.get(),4));out.push_back(std::move(p));}return out;}

domain::IntervalId SQLiteScheduleRepository::add_interval(const domain::AvailabilityInterval& i){std::scoped_lock lock(mutex_);execute("BEGIN IMMEDIATE");try{auto s=prepare(db_,"INSERT INTO availability_intervals(calendar_id,participant_id,local_date,start_minute,end_minute) VALUES(?,?,?,?,?)");sqlite3_bind_int64(s.get(),1,i.calendar_id);sqlite3_bind_int64(s.get(),2,i.participant_id);bind_text(db_,s.get(),3,date_string(i.date));sqlite3_bind_int(s.get(),4,i.start_minute);sqlite3_bind_int(s.get(),5,i.end_minute);done(db_,s.get());auto id=sqlite3_last_insert_rowid(db_);auto u=prepare(db_,"UPDATE calendars SET revision=revision+1,updated_at=CURRENT_TIMESTAMP WHERE id=?");sqlite3_bind_int64(u.get(),1,i.calendar_id);done(db_,u.get());execute("COMMIT");return id;}catch(...){execute("ROLLBACK");throw;}}
std::vector<domain::AvailabilityInterval> SQLiteScheduleRepository::list_intervals(domain::CalendarId cid,domain::LocalDate first,domain::LocalDate last){std::scoped_lock lock(mutex_);std::vector<domain::AvailabilityInterval> out;auto s=prepare(db_,"SELECT id,participant_id,local_date,start_minute,end_minute FROM availability_intervals WHERE calendar_id=? AND local_date BETWEEN ? AND ? ORDER BY local_date,start_minute");sqlite3_bind_int64(s.get(),1,cid);bind_text(db_,s.get(),2,date_string(first));bind_text(db_,s.get(),3,date_string(last));while(sqlite3_step(s.get())==SQLITE_ROW)out.push_back({sqlite3_column_int64(s.get(),0),cid,sqlite3_column_int64(s.get(),1),parse_date(sqlite3_column_text(s.get(),2)),sqlite3_column_int(s.get(),3),sqlite3_column_int(s.get(),4)});return out;}
void SQLiteScheduleRepository::remove_interval(domain::IntervalId id,domain::ParticipantId owner){std::scoped_lock lock(mutex_);auto s=prepare(db_,"DELETE FROM availability_intervals WHERE id=? AND participant_id=?");sqlite3_bind_int64(s.get(),1,id);sqlite3_bind_int64(s.get(),2,owner);done(db_,s.get());}
void SQLiteScheduleRepository::clear_participant(domain::CalendarId cid,domain::Snowflake uid){std::scoped_lock lock(mutex_);auto s=prepare(db_,"DELETE FROM participants WHERE calendar_id=? AND discord_user_id=?");sqlite3_bind_int64(s.get(),1,cid);sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(uid));done(db_,s.get());}
void SQLiteScheduleRepository::set_calendar_message(domain::CalendarId cid,domain::Snowflake gid,domain::Snowflake chid,domain::Snowflake mid,int week,bool monthly){std::scoped_lock lock(mutex_);auto s=prepare(db_,"INSERT INTO calendar_messages(calendar_id,guild_id,channel_id,message_id,displayed_week,view_mode) VALUES(?,?,?,?,?,?) ON CONFLICT(calendar_id) DO UPDATE SET guild_id=excluded.guild_id,channel_id=excluded.channel_id,message_id=excluded.message_id,displayed_week=excluded.displayed_week,view_mode=excluded.view_mode,updated_at=CURRENT_TIMESTAMP");sqlite3_bind_int64(s.get(),1,cid);sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(gid));sqlite3_bind_int64(s.get(),3,static_cast<sqlite3_int64>(chid));sqlite3_bind_int64(s.get(),4,static_cast<sqlite3_int64>(mid));sqlite3_bind_int(s.get(),5,week);sqlite3_bind_int(s.get(),6,monthly?1:0);done(db_,s.get());}
domain::EventId SQLiteScheduleRepository::create_event(const domain::Event& e){std::scoped_lock lock(mutex_);auto s=prepare(db_,"INSERT INTO events(source_calendar_id,guild_id,channel_id,creator_user_id,title,description,starts_at_utc,timezone,duration_minutes) VALUES(?,?,?,?,?,?,?,?,?)");if(e.source_calendar_id)sqlite3_bind_int64(s.get(),1,*e.source_calendar_id);else sqlite3_bind_null(s.get(),1);sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(e.guild_id));sqlite3_bind_int64(s.get(),3,static_cast<sqlite3_int64>(e.channel_id));sqlite3_bind_int64(s.get(),4,static_cast<sqlite3_int64>(e.creator_user_id));bind_text(db_,s.get(),5,e.title);bind_text(db_,s.get(),6,e.description);sqlite3_bind_int64(s.get(),7,e.starts_at_utc.time_since_epoch().count());bind_text(db_,s.get(),8,e.timezone);sqlite3_bind_int(s.get(),9,e.duration_minutes);done(db_,s.get());return sqlite3_last_insert_rowid(db_);}
void SQLiteScheduleRepository::set_attendance(const domain::AttendanceRecord& r){std::scoped_lock lock(mutex_);auto s=prepare(db_,"INSERT INTO event_attendance(event_id,discord_user_id,response) VALUES(?,?,?) ON CONFLICT(event_id,discord_user_id) DO UPDATE SET response=excluded.response,updated_at=CURRENT_TIMESTAMP");sqlite3_bind_int64(s.get(),1,r.event_id);sqlite3_bind_int64(s.get(),2,static_cast<sqlite3_int64>(r.discord_user_id));sqlite3_bind_int(s.get(),3,static_cast<int>(r.response));done(db_,s.get());}

} // namespace horelac::persistence
