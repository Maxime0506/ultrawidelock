<!-- generated documentation — edit the source, not this file -->
# `bot/src/githubOAuth.ts`

@file The GitHub half of Linked Roles. It verifies the account, not the
person, and never learns more than that.
In practice that means this only ever asks for the account's public
identity: no scope is requested in the authorize URL at all, since
`GET /user` returns `id` and `login` for the authenticating account with
no scope needed — anything broader would be more access than the feature
uses. The resulting token is used once (by the caller, in the OAuth
callback) and is never stored; only `id`/`login` persist.
Endpoints verified against docs.github.com (2026-08-04).

**used by** [`bot/src/linkedRoles.ts`](linkedRoles.ts.md)

## API

### `export async function getGithubIdentity(accessToken: string, correlationId: string): Promise<GithubIdentity | null>`
`bot/src/githubOAuth.ts:58`

Used once, at link time, then discarded by the caller — this file never
persists a GitHub token itself.

**called by** `handleGithubCallback`

### `export async function countMergedPullRequests(githubLogin: string, repo: string | undefined, githubToken: string | undefined, correlationId: string): Promise<number>`
`bot/src/githubOAuth.ts:84`

Best-effort merged-PR count for the linked account against one
configured repo, via GitHub's public Search API. Degrades to 0 on any
missing config or failure — of the five metadata fields this is the only
one behind a second OAuth account *and* an external search query, so it
is the least reliable one and never allowed to block the rest of the
push.

**called by** `pushMetadataNow`

<details><summary>Undocumented (2)</summary>

- `githubAuthorizeUrl` — tested: :github authorize url requests no scope at all — only public account identity is needed@l16
- `exchangeGithubCode` — tested: :exchange github code posts form-encoded and asks for a json response@l26; :exchange github code returns null when git hub reports an error in a 200 response@l43

</details>
