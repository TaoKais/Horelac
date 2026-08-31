# Privacy

Horelac stores only data needed to provide scheduling and attendance features.

## Stored data

- Discord guild, channel, message, creator, moderator, and participant user identifiers
- Calendar configuration, title, description, month, timezone, state, and locale
- A participant's selected identity mode and optional per-calendar alias/display name
- Date-specific availability intervals
- Events, attendance responses, and reminder job state

User IDs are required internally to enforce ownership, count each participant once, and let a user edit or erase their own submissions. Horelac does not intentionally collect IP addresses, passwords, message contents, or unrelated profile data. The Discord token is read from the process environment and is never stored in SQLite.

## Public visibility

Heatmaps and best-window results show aggregate counts. Anonymous calendars never publish participant identifiers or the owner of a time slot. Alias calendars may show only the chosen calendar-specific aliases. Discord-name mode may show display names when the calendar permits it. Internal database IDs and Discord user IDs are never intended for public output.

Planning identities do not automatically become attendance identities. Attendance visibility is handled separately.

## Retention and deletion

Data remains until a user clears their calendar data, an authorized owner deletes the calendar, or a documented deployment retention policy archives/removes it. Archival does not imply deletion. `/schedule clear` deletes the participant record and cascades its availability for that calendar. Calendar deletion cascades participants, availability, message metadata, and associated data where defined by the schema.

Operators must back up and delete backups according to their own disclosed retention policy. SQLite files and backups contain internal identifiers and must be protected as personal data.

## Logs and security

Do not log tokens, environment values, full interaction payloads, aliases unnecessarily, or raw SQL errors to public Discord responses. Restrict database and backup access to the bot operator. Report privacy/security issues privately to the repository maintainer.

