# Database

SQLite is authoritative in version 1. Startup enables foreign keys, a five-second busy timeout, and WAL mode. Migration `001_initial.sql` is also embedded in the adapter so the installed executable is not dependent on its working directory.

`schema_migrations` records applied versions. Calendars own participant, interval, and main-message records through foreign keys and cascades. Events optionally reference their source calendar but survive calendar deletion with a null source reference. Attendance has one row per event/user; absence means no response.

Intervals use ISO civil dates and half-open local-minute ranges. Concrete event starts and reminder deadlines use UTC epoch seconds while retaining an IANA timezone for display. Prepared statements bind all external values.

Indexes support open-calendar lookup, calendar/date interval aggregation, participant/date editing, Discord message recovery, upcoming guild events, and due reminders. The repository interface is intentionally domain-oriented so a PostgreSQL implementation can replace SQLite without changing services.

