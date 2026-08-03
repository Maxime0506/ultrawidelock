<!-- generated documentation — edit the source, not this file -->
# `activity/src/participants.ts`

"N watching": free social presence, no backend.
Discord synchronises no state between Activity instances, so each viewer
drives their own twin. This strip is the one honest exception: it shows who
else has the Activity open, which makes a shared session feel shared without
a sync server behind it.
Everything here is untrusted input. Discord's own documentation says not to
treat what the SDK reports client-side as truth, and a username is a string
another person chose. Nothing in this file ever reaches innerHTML.

**used by** [`activity/src/discord-boot.ts`](discord-boot.ts.md)

## API

### `export async function startParticipants(sdk: DiscordSDK): Promise<void>`
`activity/src/participants.ts:122`

Never throws and never rejects: presence is a nicety, and the twin must not
lose so much as a frame to it.

**called by** `boot`  ·  **calls** `mount`, `update`

<details><summary>Undocumented (5)</summary>

- `cleanName`
- `styleOnce`
- `mount`
- `render`
- `update`

</details>
