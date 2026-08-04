<!-- generated documentation — edit the source, not this file -->
# `bot/src/discordOAuth.ts`

@file The Discord half of Linked Roles: authorize URL, the two OAuth2
token-endpoint grants (authorization_code, refresh_token), identifying
who authorized, and pushing the final role-connection metadata. Every
endpoint and body shape here was checked against docs.discord.com
(2026-08-04) rather than assumed — this is a different trust boundary
from the rest of the bot (a real bearer credential granted by an actual
user, not just their opaque Discord ID), so it is worth being sure.
`identify` is requested alongside `role_connections.write`: the metadata
push endpoint is scoped to "whoever this access token belongs to", so the
callback needs `GET /users/@me` to learn *which* Discord user just
authorized before it can store anything against them.

**used by** [`bot/src/linkedRoles.ts`](linkedRoles.ts.md)

## API

### `export async function getDiscordUserId(accessToken: string, correlationId: string): Promise<string | null>`
`bot/src/discordOAuth.ts:96`

Who an access token belongs to — never a username, only the ID, matching
every other identity this bot stores.

**called by** `handleDiscordCallback`

### `export async function pushRoleConnection(accessToken: string, applicationId: string, metadata: Record<string, string>, correlationId: string): Promise<boolean>`
`bot/src/discordOAuth.ts:115`

The final step: hands Discord the stringified metadata for whoever
`accessToken` belongs to. Uses the user's own bearer token, not the bot
token — this is a user-scoped endpoint by design.

**called by** `pushMetadataNow`

<details><summary>Undocumented (3)</summary>

- `discordAuthorizeUrl` — tested: write, the client id, redirect and state@l25
- `tokenRequest`
- `exchangeDiscordCode` — tested: :exchange discord code pos ts the authorization code grant form-encoded and computes an absolute expiry@l36; :exchange discord code returns null (not a throw) on a non-2xx response@l56

</details>
