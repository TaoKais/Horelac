CREATE TABLE IF NOT EXISTS schema_migrations (
    version INTEGER PRIMARY KEY,
    name TEXT NOT NULL,
    applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS guilds (
    guild_id INTEGER PRIMARY KEY,
    locale TEXT NOT NULL DEFAULT 'en',
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS channels (
    channel_id INTEGER PRIMARY KEY,
    guild_id INTEGER NOT NULL REFERENCES guilds(guild_id) ON DELETE CASCADE,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS calendars (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    guild_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    creator_user_id INTEGER NOT NULL,
    title TEXT NOT NULL CHECK(length(title) BETWEEN 1 AND 100),
    description TEXT NOT NULL DEFAULT '' CHECK(length(description) <= 1000),
    year INTEGER NOT NULL,
    month INTEGER NOT NULL CHECK(month BETWEEN 1 AND 12),
    timezone TEXT NOT NULL,
    start_minute INTEGER NOT NULL,
    end_minute INTEGER NOT NULL,
    slot_minutes INTEGER NOT NULL,
    week_start INTEGER NOT NULL,
    identity_modes INTEGER NOT NULL,
    default_identity_mode INTEGER NOT NULL,
    cell_value_mode INTEGER NOT NULL DEFAULT 0,
    state INTEGER NOT NULL DEFAULT 0,
    locale TEXT NOT NULL DEFAULT 'en',
    revision INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS calendar_moderators (
    calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,
    user_id INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(calendar_id, user_id)
);
CREATE TABLE IF NOT EXISTS participants (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,
    discord_user_id INTEGER NOT NULL,
    identity_mode INTEGER NOT NULL,
    alias TEXT,
    alias_key TEXT,
    display_name TEXT,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(calendar_id, discord_user_id),
    UNIQUE(calendar_id, alias_key)
);
CREATE TABLE IF NOT EXISTS availability_intervals (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    calendar_id INTEGER NOT NULL REFERENCES calendars(id) ON DELETE CASCADE,
    participant_id INTEGER NOT NULL REFERENCES participants(id) ON DELETE CASCADE,
    local_date TEXT NOT NULL,
    start_minute INTEGER NOT NULL,
    end_minute INTEGER NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    CHECK(start_minute >= 0 AND end_minute <= 1440 AND start_minute < end_minute)
);
CREATE TABLE IF NOT EXISTS calendar_messages (
    calendar_id INTEGER PRIMARY KEY REFERENCES calendars(id) ON DELETE CASCADE,
    guild_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    message_id INTEGER NOT NULL,
    displayed_week INTEGER NOT NULL DEFAULT 0,
    view_mode INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS events (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_calendar_id INTEGER REFERENCES calendars(id) ON DELETE SET NULL,
    guild_id INTEGER NOT NULL,
    channel_id INTEGER NOT NULL,
    creator_user_id INTEGER NOT NULL,
    title TEXT NOT NULL,
    description TEXT NOT NULL DEFAULT '',
    starts_at_utc INTEGER NOT NULL,
    timezone TEXT NOT NULL,
    duration_minutes INTEGER NOT NULL CHECK(duration_minutes > 0),
    state INTEGER NOT NULL DEFAULT 0,
    message_id INTEGER,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE TABLE IF NOT EXISTS event_attendance (
    event_id INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    discord_user_id INTEGER NOT NULL,
    response INTEGER NOT NULL,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY(event_id, discord_user_id)
);
CREATE TABLE IF NOT EXISTS reminder_jobs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    event_id INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    due_at_utc INTEGER NOT NULL,
    kind INTEGER NOT NULL,
    state INTEGER NOT NULL DEFAULT 0,
    attempts INTEGER NOT NULL DEFAULT 0,
    updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
CREATE INDEX IF NOT EXISTS idx_calendars_guild_channel_state
    ON calendars(guild_id, channel_id, state);
CREATE INDEX IF NOT EXISTS idx_intervals_calendar_date
    ON availability_intervals(calendar_id, local_date, start_minute, end_minute);
CREATE INDEX IF NOT EXISTS idx_intervals_participant_date
    ON availability_intervals(participant_id, local_date);
CREATE INDEX IF NOT EXISTS idx_calendar_messages_message ON calendar_messages(message_id);
CREATE INDEX IF NOT EXISTS idx_events_guild_start ON events(guild_id, starts_at_utc);
CREATE INDEX IF NOT EXISTS idx_reminders_due ON reminder_jobs(state, due_at_utc);

