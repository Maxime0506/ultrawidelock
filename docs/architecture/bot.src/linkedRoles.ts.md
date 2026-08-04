<!-- generated documentation — edit the source, not this file -->
# `bot/src/linkedRoles.ts`

@file Orchestrates the Linked Roles flow across the three plain browser
routes (`/linked-role`, `/discord-oauth-callback`, `/github-oauth-callback`)
that `src/index.ts` registers alongside — but structurally separate
from — the signed interactions endpoint. These are ordinary redirects a
browser follows, not Discord interactions: no Ed25519 signature is
involved or expected, which is also why they live on their own routes
rather than folded into `POST /`.
Each exported function takes the incoming request's own URL and derives
both OAuth redirect URIs from its origin, so no separate "base URL"
secret is needed — the Worker always knows where it is being reached at.
Metadata is only ever pushed once, right after both OAuth legs complete.
There is no scheduled refresh: re-running `/linked-role` is how a
contributor updates their badge later (a periodic full refresh across
every linked user is a natural follow-up, not built here, since it would
mean spending every linked user's share of GitHub's search rate limit on
every sweep tick whether or not anything about them changed).

**depends on** [`bot/src/discordOAuth.ts`](discordOAuth.ts.md), [`bot/src/env.ts`](env.ts.md), [`bot/src/githubOAuth.ts`](githubOAuth.ts.md), [`bot/src/oauthLinks.ts`](oauthLinks.ts.md), [`bot/src/oauthState.ts`](oauthState.ts.md), [`bot/src/roleConnection.ts`](roleConnection.ts.md)  ·  **used by** [`bot/src/index.ts`](index.ts.md)

## API

### `async function pushMetadataNow(env: Env, discordUserId: string, githubLogin: string, correlationId: string): Promise<boolean>`
`bot/src/linkedRoles.ts:181`

Computes and pushes the five metadata fields with the access token the
Discord leg parked, then scrubs it. Returns whether the push itself
succeeded — the GitHub link is already saved by this point regardless.

**called by** `handleGithubCallback`  ·  **calls** `computeRegistryMetadata`, `countMergedPullRequests`, `decryptedAccessToken`, `getLink`, `markMetadataPushed`, `pushRoleConnection`, `scrubLinkSecrets`, `stringifyMetadata`

<details><summary>Undocumented (6)</summary>

- `htmlPage`
- `errorPage`
- `callbackUrl`
- `startLinkedRole`
- `handleDiscordCallback`
- `handleGithubCallback`

</details>
