# Horelac Architecture

Status: proposed for review  
Target: first production-oriented release  
Primary language: C++20

## 1. Goals and architectural principles

Horelac is a reusable Discord scheduling service. It collects participant availability for a configured month, aggregates it without double-counting participants, renders weekly and monthly heatmaps, ranks continuous event windows, and converts chosen windows into attendance-tracked events.

The design follows these rules:

- The domain and scheduling algorithms do not depend on DPP, SQLite, Cairo, or Discord types.
- SQLite is the source of truth. Caches and interaction sessions are disposable.
- Public output is aggregate-first and respects each calendar's allowed identity modes.
- All durable actions are restart-safe and use compact persistent component identifiers.
- Expensive rendering and Discord message edits run outside interaction callbacks.
- Repository interfaces allow a future PostgreSQL adapter and a future REST API.

## 2. Layered architecture

```text
Discord/DPP adapter          Future REST adapter
        |                            |
        +--------- application ------+
                       |
              ScheduleService
          /        |         \
 authorization  algorithms  localization
          |        |         |
          +------ domain ----+
                 /      \
       repository ports  renderer port
             |                |
       SQLite adapter      Cairo adapter
```

### Domain

Pure value types, invariants, date/slot calculations, heatmap aggregation, best-window ranking, calendar state rules, and attendance state. It has no knowledge of Discord interactions or SQL rows.

### Application

Use cases coordinated by `ScheduleService`, `EventService`, authorization policies, validation, transactions, cache invalidation, and render invalidation. Application results use typed errors such as `CalendarNotFound`, `PermissionDenied`, `InvalidTimeRange`, `CalendarClosed`, and `AliasAlreadyUsed`.

### Ports

Small interfaces such as `IScheduleRepository`, `IEventRepository`, `IUnitOfWork`, `IScheduleRenderer`, `IClock`, and `ILocalizationService`. Interfaces express domain operations rather than generic CRUD or SQL details.

### Adapters

- DPP maps slash commands/components/modals to application requests and maps results to localized responses.
- SQLite implements repositories and migrations with prepared statements and transactions.
- Cairo converts render-neutral heatmap models to in-memory PNG bytes.
- Configuration, logging, and timezone adapters are infrastructure concerns.

## 3. Proposed directory structure

```text
Horelac/
|-- CMakeLists.txt
|-- CMakePresets.json
|-- README.md
|-- LICENSE
|-- CONTRIBUTING.md
|-- .clang-format
|-- .clang-tidy
|-- .env.example
|-- .gitignore
|-- Dockerfile
|-- docker-compose.yml
|-- cmake/
|-- include/horelac/
|   |-- application/
|   |-- bot/
|   |-- domain/
|   |-- persistence/
|   |-- render/
|   |-- services/
|   `-- support/
|-- src/
|   |-- application/
|   |-- bot/
|   |-- persistence/sqlite/
|   |-- render/cairo/
|   |-- services/
|   |-- support/
|   `-- main.cpp
|-- migrations/
|-- tests/
|   |-- unit/
|   |-- integration/
|   `-- fixtures/
|-- assets/
|-- locales/en.json
|-- locales/es.json
|-- docs/
|   |-- architecture.md
|   |-- database.md
|   |-- deployment.md
|   `-- privacy.md
`-- .github/workflows/build.yml
```

Public headers live under a single `horelac` namespace path. Adapter-specific headers that are not public remain beside their source files.

## 4. Domain model

Identifiers are strong wrappers around 64-bit integers or Discord snowflakes so unrelated IDs cannot be mixed accidentally. Discord snowflakes remain adapter/application data and are never rendered publicly.

### Calendar

- `CalendarId`
- guild, channel, creator, and current main-message identifiers
- title and description
- `YearMonth`
- IANA timezone name
- local start/end minutes and slot resolution
- configurable week start, Monday by default
- allowed identity mode bitset and default mode
- cell value display mode
- locale
- state: `Open`, `Closed`, or `Archived`
- creation/update timestamps and optimistic revision

### Participant

- `ParticipantId`, `CalendarId`, internal Discord user snowflake
- selected identity mode
- optional validated per-calendar alias
- optional last-known display name, stored only when Discord-name mode is used
- creation/update timestamps

A unique `(calendar_id, discord_user_id)` constraint permits users to manage their own data. Alias uniqueness is case-folded within a calendar when enabled.

### AvailabilityInterval

- interval ID, calendar ID, participant ID
- local calendar date
- start/end local minutes, represented as a half-open range `[start, end)`
- audit timestamps

Intervals are date-specific after repeat rules are expanded. This makes month boundaries, partial weeks, and per-week editing explicit.

### Heatmap

`WeeklyHeatmap` contains dates and ordered `HeatmapCell` values. Each cell contains local date, slot start/end, available participant count, total participant count, and percentage. `MonthlySummary` contains each date's peak count and best configured-duration window.

### Event and attendance

An `Event` references its source calendar optionally but stores its own UTC start instant, timezone, local presentation data, title, duration, state, creator, and Discord message reference. `AttendanceRecord` stores one response per event/user: `Attending`, `Maybe`, or `CannotAttend`. Missing rows mean no response. Planning availability and actual attendance are separate aggregates.

## 5. SQLite schema

Migration SQL will be embedded or copied into the runtime image and applied in order inside transactions. `schema_migrations(version, name, applied_at)` records successful migrations. Every connection enables `PRAGMA foreign_keys = ON`, a bounded busy timeout, and WAL mode where supported.

Principal tables:

- `guilds(guild_id PRIMARY KEY, locale, created_at, updated_at)`
- `channels(channel_id PRIMARY KEY, guild_id REFERENCES guilds, created_at)`
- `calendars(id PRIMARY KEY, guild_id, channel_id, creator_user_id, title, description, year, month, timezone, start_minute, end_minute, slot_minutes, week_start, identity_modes, default_identity_mode, cell_value_mode, state, locale, revision, created_at, updated_at)`
- `calendar_moderators(calendar_id, user_id, created_at, PRIMARY KEY(calendar_id, user_id))`
- `participants(id PRIMARY KEY, calendar_id REFERENCES calendars ON DELETE CASCADE, discord_user_id, identity_mode, alias, alias_key, display_name, created_at, updated_at, UNIQUE(calendar_id, discord_user_id))`
- `availability_intervals(id PRIMARY KEY, calendar_id, participant_id REFERENCES participants ON DELETE CASCADE, local_date, start_minute, end_minute, created_at, updated_at, CHECK(start_minute < end_minute))`
- `calendar_messages(calendar_id PRIMARY KEY, guild_id, channel_id, message_id, displayed_week, view_mode, updated_at)`
- `events(id PRIMARY KEY, source_calendar_id, guild_id, channel_id, creator_user_id, title, description, starts_at_utc, timezone, duration_minutes, state, message_id, created_at, updated_at)`
- `event_attendance(event_id, discord_user_id, response, updated_at, PRIMARY KEY(event_id, discord_user_id))`
- `reminder_jobs(id PRIMARY KEY, event_id, due_at_utc, kind, state, attempts, updated_at)`

Important indexes include calendar lookup by guild/channel/state, intervals by calendar/date, intervals by participant/date, messages by message ID, events by guild/start instant, and pending reminders by state/due instant. Foreign keys that cross both calendar and participant are validated by repository logic and, where practical, composite constraints. No SQL is assembled from user input; all values are bound to prepared statements.

Repository transactions cover participant upsert plus interval changes, calendar deletion, event creation, and attendance updates. SQLite exceptions are translated into application errors without leaking SQL or internal IDs to Discord.

## 6. Month, week, and interval calculations

A month is represented by its first and last civil dates. Display weeks are intersections between the configured week boundary and that month. Therefore a month produces four, five, or six display weeks and partial first/last weeks naturally.

Intervals are validated against the calendar month, configured daily time range, and slot resolution. End is exclusive. Repeat rules are application commands expanded to concrete dates inside one transaction. Overlapping intervals from one participant may be retained for edit fidelity; aggregation deduplicates them.

The initial release supports 30-minute slots in the UI while core calculations accept a validated `slot_minutes` value so 15 and 60 minutes can be enabled without redesign.

## 7. Heatmap aggregation algorithm

For a requested display week:

1. Generate valid dates in the month/week intersection and configured daily slots.
2. Fetch intervals intersecting those dates in one indexed query.
3. Group intervals by `(participant_id, date)`.
4. Convert each participant's intervals to slot coverage and set bits in a per-participant slot bitset.
5. Add each set bit once to the cell count.
6. Compute percentage using the number of distinct registered participants. A calendar can separately report contributors if product wording needs that distinction.

This is approximately `O(intervals + participants * covered_slots)` and prevents duplicate counting when a participant submits overlapping intervals. It avoids rescanning every interval for every cell. Cache entries are keyed by calendar ID, revision, and display week.

Intensity uses a normalized continuous score. The default visual score is `count / max(1, weekly_peak)` so the strongest overlap reaches maximum intensity even for large communities. The legend also reports absolute count and percentage, preventing a small weekly peak from looking deceptively strong. A renderer option may use percentage normalization when administrators prefer population-relative coloring. A perceptually ordered color ramp and minimum visible alpha preserve low counts.

## 8. Best-time-window algorithm

Input includes a heatmap, duration, result limit, optional minimum count/percentage, and optionally allowed dates/times. Duration must be positive and a multiple of slot resolution.

For each date, slide a window of `duration / slot_minutes` consecutive cells. A candidate is scored lexicographically by:

1. highest minimum availability across all cells (conservative attendance guarantee),
2. highest average availability,
3. lowest variance (stable participation),
4. earlier local start time and date for deterministic output.

Candidates failing thresholds are discarded. After sorting, non-maximum suppression removes substantially overlapping candidates on the same date; by default a candidate is skipped when it overlaps an already selected result by 50% or more. This avoids returning nearly identical shifted windows while allowing distinct choices. Results include minimum, rounded average, total participants, local range, and timezone.

The algorithm is pure domain code and receives no Discord object.

## 9. Discord command architecture

DPP command registration and callback signatures will be verified against a pinned DPP release before Phase 2/8 implementation. Commands use grouped subcommands where Discord permits:

- `/schedule create|open|add|edit|remove|clear|view|month|best|participants|close|delete|restore-message`
- `/event create-from-schedule|attendance`
- `/admin schedule-config`

Handlers are thin:

1. acknowledge/defer within Discord's interaction deadline;
2. parse and validate adapter input;
3. construct an authenticated application request containing actor/guild/channel context;
4. call the service;
5. localize a success or safe error response;
6. enqueue render invalidation when state changes.

Authorization policies distinguish owner, configured calendar moderator, guild permission-based moderator, and participant. Elevated operations never rely only on hidden UI controls; every service command rechecks authorization.

The bot requests only `bot` and `applications.commands` scopes and the permissions View Channels, Send Messages, Embed Links, Attach Files, and Use Application Commands. Read Message History is added only if message-recovery implementation proves it necessary. Administrator and Manage Messages are not required.

## 10. Components and modal interaction flow

Persistent custom IDs use a versioned compact format such as `hsb:v1:<action>:<calendar-token>:<argument>`. The calendar token is a non-sensitive opaque encoded ID with length and character validation. No user ID, alias, token, or other private data appears in component IDs. All referenced state is reloaded from SQLite after restart.

Primary message actions:

- Add Availability
- View Calendar
- My Schedule
- Participants
- Settings (shown or authorized server-side for moderators)

Availability flow:

1. select display week;
2. select one or more dates/weekdays;
3. select or enter validated start/end times;
4. choose current week or every applicable week;
5. choose identity mode/alias if not already configured;
6. show an ephemeral confirmation and save transactionally;
7. enqueue one debounced calendar render.

Because Discord select menus have option/component limits, time options are paged or collected in a small modal when the configured range is too large. Ephemeral responses show private schedules. Edit/remove uses durable interval IDs resolved server-side but never displays internal IDs. Week navigation updates the primary message's selected view; user-specific browsing may use ephemeral messages to avoid navigation races. The exact shared-versus-ephemeral navigation behavior will be validated during UX implementation.

## 11. Image rendering pipeline

`IScheduleRenderer` accepts domain/render DTOs and returns PNG bytes plus dimensions; it does not accept Discord types. `CairoScheduleRenderer` renders in memory through a Cairo PNG stream callback, avoiding temporary-file accumulation.

Weekly output includes title, week index/date range, localized weekday/date headers, time rows, grid cells, dynamic heat colors, legend, participant count, timezone, and optional cell values. Layout starts near 1200x800 but derives height from slot count and applies a maximum dimension policy appropriate for Discord. Monthly output uses a conventional calendar grid whose day cells show peak availability and the day's best window/mini intensity indicator.

After rendering, the bot adapter uploads bytes and edits the stored main message. Only after a successful Discord edit is the rendered revision considered published. Failures use bounded exponential retry respecting Discord rate limits; a stale render is discarded if a newer calendar revision exists.

## 12. Concurrency and render queue

Discord callbacks never perform Cairo rendering or long SQLite work directly. A bounded worker queue handles CPU/I/O work. Database writes use short transactions and a controlled connection strategy appropriate for SQLite (initially one serialized writer plus read connections).

`RenderQueue` maintains per-calendar state:

- dirty revision
- debounce deadline (default configurable, approximately three seconds)
- queued/running flag
- latest requested view

Marking an already dirty calendar updates its revision/deadline rather than adding another job. One calendar renders at a time, while a small worker pool may render different calendars concurrently. Per-calendar serialization prevents older output overwriting newer output. Shared maps are protected by scoped mutexes; locks are never held during rendering, database I/O, or Discord network calls.

On SIGINT/SIGTERM the application stops accepting new work, stops the Discord client, drains or safely cancels bounded jobs, joins workers, checkpoints/closes SQLite, and flushes logs.

## 13. Cache strategy

SQLite remains authoritative. A bounded LRU/TTL cache stores calendar configuration and optionally computed heatmaps. Keys include calendar revision so stale entries cannot be mistaken for current state.

Successful mutations increment `calendars.revision` in the same transaction, then invalidate calendar metadata, heatmaps, monthly summaries, and participant-list cache entries. Cross-process cache coherence is not required for the single-instance SQLite release. A future multi-instance/PostgreSQL deployment will use revision checks plus an invalidation channel.

## 14. Privacy and anonymity

Public calendar output is aggregate-first. Discord user IDs and database IDs never appear in images, embeds, component IDs, logs intended for routine operation, or public participant responses.

- Anonymous mode: only counts are public. Internal user linkage exists solely for ownership/edit/delete, authorization, abuse protection, and deduplication.
- Alias mode: only the validated per-calendar alias is public where participant listing is enabled.
- Discord-name mode: the current or last refreshed display name may be shown only when allowed by calendar policy and selected by the participant.

Data minimization rules include bounded text fields, no interaction-payload logging, no token logging, no IP collection, and no storage of data unrelated to scheduling. `/schedule clear` removes a user's participant row and cascades their intervals for that calendar. Administrative calendar deletion cascades planning data after explicit confirmation. Archived calendars are retained until user/admin deletion or a documented optional retention rule. The later `docs/privacy.md` will describe stored fields, purpose, visibility, retention, and deletion in user-facing language.

Participant listings are generated through a privacy policy object so a handler cannot accidentally bypass identity rules. Event attendance has a separate visibility policy and never inherits anonymous planning identities automatically.

## 15. Timezone strategy

Calendar configuration uses validated IANA names such as `Europe/Madrid`. Civil calendar dates and recurring availability are stored as local date plus local minutes because they express wall-clock intent. Concrete events and reminders are stored as UTC instants with their originating IANA timezone retained for display.

The implementation will use Howard Hinnant's `date/tz` library pinned through CMake unless build-toolchain verification proves complete C++20 `std::chrono` timezone support on all supported compilers. This gives consistent IANA TZDB behavior across Linux CI, Docker, and supported local builds.

For availability slots on DST transition dates, local-to-system conversion explicitly handles nonexistent and ambiguous times. Nonexistent local times are rejected with a friendly message; ambiguous event times require a defined choice (default earliest offset with explicit confirmation) rather than silent guessing. Pure heatmap aggregation remains in civil local time; UTC conversion occurs when creating a concrete event/reminder.

## 16. Configuration, logging, and rate protection

Configuration is loaded once from environment variables: `DISCORD_TOKEN`, `DISCORD_APPLICATION_ID`, `DATABASE_PATH`, `LOG_LEVEL`, `DEFAULT_LOCALE`, `RENDER_OUTPUT_DIR`, and optional `DISCORD_DEV_GUILD_ID`. Required values are validated before connecting. Secrets are held only as long as needed and are never emitted.

Structured logging uses levels and stable event names with redacted/small context. Rate protection uses token buckets keyed by user and guild for calendar creation and costly commands. Rendering is additionally protected by debounce, queue bounds, and one pending job per calendar. Discord REST operations rely on DPP's rate-limit handling plus application-level coalescing; no polling loop is used.

## 17. Docker and deployment

A multi-stage Debian-based image builds the pinned dependencies and C++ binary. The runtime stage contains only required shared libraries, CA certificates, timezone data, locales/assets/migrations, and the executable. It runs as an unprivileged user with `/data` as the SQLite volume and uses `DATABASE_PATH=/data/horelac.db`.

`docker-compose.yml` loads environment configuration without embedding secrets, mounts a named data volume, restarts on failure, and sends SIGTERM with a sufficient stop grace period. A lightweight health strategy initially checks the process plus an application-maintained health marker reflecting database migration success and Discord readiness; a tiny HTTP endpoint will only be introduced if the selected deployment platforms require it.

GitHub stores and builds source code; it does not host the continuously running bot. Deployment documentation will cover a VPS, Docker host, home server, or trusted container platform.

## 18. Testing strategy

Catch2 is proposed for concise unit and integration tests. Tests are divided into:

- pure unit tests for dates, leap years, partial weeks, interval validation, repeated dates, deduplication, heatmaps, ranking/non-overlap suppression, authorization, privacy projections, component-ID parsing, and input bounds;
- timezone tests using a pinned TZDB for spring-forward gaps and fall-back ambiguity;
- SQLite integration tests using a unique temporary database per test process, real migrations, foreign keys, transactions, cascades, uniqueness, deletion, and restart/reopen behavior;
- renderer smoke/golden tests validating PNG signatures, dimensions, and selected pixel/layout invariants without brittle whole-image comparisons;
- adapter tests around command parsing and localized error mapping, with Discord network calls behind a narrow gateway interface.

CI builds with GCC and optionally Clang on Ubuntu, enables `-Wall -Wextra -Wpedantic` for project targets, runs `ctest --output-on-failure`, and fails on configure/build/test errors. Sanitizer jobs can be added after the baseline build is stable.

## 19. PostgreSQL migration path

Repositories expose transaction-scoped domain operations, not SQLite handles. IDs, timestamps, constraints, and error semantics are normalized in the application layer. `SQLiteScheduleRepository` owns SQL and row mapping. A future `PostgresScheduleRepository` can implement the same ports using a connection pool without changes to the domain, renderer, or Discord handlers.

SQLite-specific pragmas, migration syntax, and busy handling remain inside the adapter. Migration files may have database-specific variants later. Multi-instance deployment will also replace in-memory render invalidation with a durable job queue or PostgreSQL advisory-lock/outbox design.

## 20. Future REST API and web dashboard

The future REST adapter will authenticate requests and translate HTTP DTOs into the same application commands used by Discord. It will never call DPP handlers or access SQLite directly. Service commands carry an abstract actor/context and authorization capabilities, allowing Discord roles and web accounts to map into the same policies.

Read models such as weekly heatmaps, monthly summaries, participant privacy projections, and best windows are transport-neutral. Renderer output can be returned by Discord, HTTP, or a CLI. If web/API writes are added, an application outbox can publish render/message refresh work without coupling the service to Discord.

## 21. Incremental implementation and build gates

Work proceeds through the requested phases. Each phase must leave the repository buildable for its implemented scope. The standard verification gate is:

```text
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Phase 2 will pin and verify the actual DPP API and produce the minimal connecting bot. Subsequent phases add migrations, domain, services, algorithms, adapters, rendering, and operations without moving core logic into Discord handlers. A phase report will list created/modified/deleted files, tests and build results, and significant decisions. Logical commits will be created only when Git is available and the repository is initialized.

## 22. Decisions requiring approval

The proposal makes these concrete choices:

1. Project/repository and C++ namespace name: `Horelac` / `horelac`.
2. Monday is the default week start; configuration can support Sunday later without changing storage.
3. Date-specific intervals are authoritative; repeat rules expand transactionally.
4. Best-window ranking prioritizes minimum overlap, then average, then stability.
5. Cairo renders PNG directly to memory.
6. Howard Hinnant `date/tz` is the cross-toolchain timezone baseline unless verified standard-library support is sufficient.
7. Catch2 is the test framework.
8. Shared primary-message navigation is persisted; private/user-specific views may be ephemeral to prevent navigation races.
9. SQLite is optimized for a single bot instance; multi-instance operation is deferred to PostgreSQL/durable queue work.

Major implementation should start only after this architecture and the decisions above are accepted or revised.
