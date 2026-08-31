# Install Horelac in Discord — v1.2

Horelac is a self-hosted Discord application. Installing it has two distinct parts:

1. invite the Discord application to a server;
2. keep the Horelac container running on a host.

## Create the Discord application

1. Visit <https://discord.com/developers/applications> and select **New Application**.
2. Name it `Horelac` and open **Bot**.
3. Select **Reset Token**, copy the token once, and save it as `DISCORD_TOKEN` in the
   hosting provider's secret manager. Never paste it into GitHub source files.
4. Copy the **Application ID** from General Information and save it as
   `DISCORD_APPLICATION_ID`.

Horelac uses slash commands and interactions. It does not need Message Content, Server
Members, or Presence privileged intents.

## Invite it to a server

Replace `YOUR_APPLICATION_ID` below and open the resulting link while signed into Discord:

```text
https://discord.com/oauth2/authorize?client_id=YOUR_APPLICATION_ID&scope=bot%20applications.commands&permissions=2147601408
```

Select a server where you have permission to manage integrations and approve the listed
permissions. Never grant Administrator. If you do not need automatic message recovery,
Read Message History can be removed in the Developer Portal URL Generator.

## Run the bot

On a Docker host:

```sh
cp .env.example .env
# Set DISCORD_TOKEN and DISCORD_APPLICATION_ID in .env.
docker compose up -d --build
docker compose logs -f horelac
```

For development, set `DISCORD_DEV_GUILD_ID` to your test server ID. Commands are then
registered only in that server and appear quickly. Remove it for global production command
registration.

The bot must remain running somewhere; inviting it does not cause Discord or GitHub to host
the executable.

## Security checklist

- Keep the GitHub repository private while it contains unfinished release code.
- Store the bot token only as a secret/environment variable.
- If a token is ever exposed, reset it immediately in the Developer Portal.
- Mount `/data` persistently and protect backups of `horelac.db`.
- Use a separate Discord test server before deploying to a community server.
