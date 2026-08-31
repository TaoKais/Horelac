# Horelac

**Release:** v1.2.0

Horelac is a reusable Discord availability planner written in C++20. Members submit monthly availability anonymously, with a calendar-specific alias, or with a Discord display name. The scheduling core aggregates submissions into weekly heatmaps and ranks continuous event windows conservatively.

## Features

- Month calendars split into correct Monday-based partial weeks
- Configurable 15/30/60-minute scheduling core (30 minutes recommended initially)
- Same-participant overlap deduplication
- Anonymous, alias, and Discord-name identity policies
- SQLite persistence with migrations, prepared statements, foreign keys, transactions, and indexes
- Conservative best-window ranking by minimum overlap, average overlap, and stability
- Cairo PNG heatmap renderer separated from Discord
- Debounced, thread-safe per-calendar render queue
- Persistent versioned component identifiers
- Event and attendance persistence prepared for reminders
- English and Spanish localization foundation
- Catch2 tests, Docker image, and GitHub Actions CI

## Architecture

Discord is an adapter around transport-neutral application services. Core scheduling code does not depend on DPP, Cairo, or SQLite. See [architecture](docs/architecture.md), [database](docs/database.md), [privacy](docs/privacy.md), and [deployment](docs/deployment.md).

## Requirements

- CMake 3.24+
- C++20 compiler (GCC 13+ or Clang 16+ recommended)
- SQLite3 development package
- Cairo and pkg-config
- Git and network access during first configure (DPP and Catch2 are pinned with FetchContent)

## Build from source

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Copy `.env.example` to `.env`, fill the required values, then run the binary from an environment that loads those variables. `.env` is intentionally ignored; the binary does not parse dotenv files itself.

## Docker

```sh
cp .env.example .env
# Edit .env and set DISCORD_TOKEN and DISCORD_APPLICATION_ID.
docker compose up -d --build
docker compose logs -f horelac
```

SQLite is persisted in the `horelac-data` volume at `/data/horelac.db`. The final container runs as an unprivileged user. GitHub hosts the source and CI; it does **not** keep the bot running. Run the container on a VPS, Docker host, home server, cloud VM, or trusted container platform.

## Configuration

| Variable | Required | Default | Meaning |
|---|---:|---|---|
| `DISCORD_TOKEN` | yes | â€” | Bot token; never log or commit it |
| `DISCORD_APPLICATION_ID` | yes | â€” | Discord application snowflake |
| `DISCORD_DEV_GUILD_ID` | no | â€” | Register commands instantly in one development guild |
| `DATABASE_PATH` | no | `./data/horelac.db` | SQLite file path |
| `LOG_LEVEL` | no | `info` | Safe log verbosity |
| `DEFAULT_LOCALE` | no | `en` | Fallback UI locale |
| `RENDER_OUTPUT_DIR` | no | `./rendered` | Reserved fallback render directory |

Without `DISCORD_DEV_GUILD_ID`, commands register globally and may take longer to propagate.

## Discord Developer Portal setup

1. Open the Discord Developer Portal and create an application.
2. Open **Bot**, create the bot, reset/copy its token, and store it only in `DISCORD_TOKEN`.
3. Copy the Application ID into `DISCORD_APPLICATION_ID`.
4. In OAuth2 URL Generator select scopes `bot` and `applications.commands`.
5. Select only View Channels, Send Messages, Embed Links, Attach Files, and Use Application Commands.
6. Open the generated URL and invite the bot to your server.

For a direct installation link, replace `YOUR_APPLICATION_ID` in this URL:

```text
https://discord.com/oauth2/authorize?client_id=YOUR_APPLICATION_ID&scope=bot%20applications.commands&permissions=2147601408
```

The numeric permission set requests only View Channels, Send Messages, Embed Links,
Attach Files, Read Message History, and Use Application Commands. Review the permissions
shown by Discord before authorizing. See [Discord installation](docs/discord-install.md).

Do not grant Administrator. Read Message History and Manage Messages are not required by the current implementation.

## Commands

- `/schedule create` creates an anonymous monthly calendar and its main message.
- `/schedule add` submits a date-specific interval; supplying an alias selects alias mode.
- `/schedule view` shows current weekly aggregate metadata.
- `/schedule best duration:<minutes>` ranks continuous windows.
- `/schedule clear` removes the caller's data for a calendar.

The application layer and schema also support state changes, identities, participant projections, events, attendance, durable messages, and reminder jobs. The interactive surface is intentionally versioned so additional subcommands/modals can be introduced without invalidating existing buttons.

## Privacy

Public views are aggregate-first. Anonymous schedules never expose Discord user IDs or slot ownership. Internal user linkage exists only so users can edit/delete their data, for deduplication, and for authorization. Use `/schedule clear` to remove personal data; calendar owners can delete the calendar and cascading records. See [privacy policy](docs/privacy.md).

## Testing

Tests cover partial/leap months, interval deduplication, best-window ranking, invalid inputs, anonymous projections, migrations, persistence, and cascading deletion. CI fails on configuration, compilation, or test failures.

## Current release scope

This repository provides the production-oriented foundation and initial Discord command path. PNG generation, event persistence, render debounce, monthly models, and privacy abstractions exist as separated components; richer Discord modal/navigation/attendance presentation should be expanded against the pinned DPP API without moving business rules into handlers.

## Contributing

Read [CONTRIBUTING.md](CONTRIBUTING.md). Keep domain code free of Discord types, use prepared SQL, add tests for behavior changes, and never commit credentials or databases.

## License

MIT. See [LICENSE](LICENSE).

