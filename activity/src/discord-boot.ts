/* Discord Activity boot shim for the openaliro web twin.
 *
 * The twin is a standalone page that knows nothing about Discord and must keep
 * working when opened straight off disk. So this file is the entire Discord
 * surface: it detects the embedded context, marks the document so CSS can
 * adapt, and completes the SDK handshake. It does not touch the simulation, it
 * does not request an OAuth scope, and it holds no secret -- the client id is
 * public by design and is injected at build time.
 *
 * Anything beyond `ready()` belongs in a later phase.
 */

import { DiscordSDK } from "@discord/embedded-app-sdk";

import { startParticipants } from "./participants";

/* Discord launches the Activity with frame_id in the query string. That is the
 * documented signal, and unlike a user-agent test it cannot be spoofed into a
 * false negative by a client we have not seen. Absent it, we are a normal web
 * page and do nothing at all. */
function discordFrameId(): string | null {
  try {
    return new URLSearchParams(window.location.search).get("frame_id");
  } catch {
    return null;
  }
}

async function boot(): Promise<void> {
  if (!discordFrameId()) return;

  const root = document.documentElement;
  /* Set before awaiting anything: the handshake takes a round trip, and CSS
   * should not spend it laid out for a standalone browser tab. */
  root.setAttribute("data-in-discord", "connecting");

  const clientId = import.meta.env.VITE_DISCORD_CLIENT_ID;
  if (!clientId) {
    root.setAttribute("data-in-discord", "unconfigured");
    console.warn(
      "openaliro: running inside Discord but VITE_DISCORD_CLIENT_ID was not set at build time; " +
        "the twin still works, the SDK handshake is skipped. See activity/README.md.",
    );
    return;
  }

  try {
    const sdk = new DiscordSDK(clientId);
    await sdk.ready();
    root.setAttribute("data-in-discord", "ready");
    /* Presence is deliberately not awaited into the ready path: it renders a
     * headcount, and nothing about the twin should wait on it. */
    void startParticipants(sdk);
  } catch (e) {
    /* A failed handshake must not take the twin down with it. The simulation
     * is entirely local and does not need Discord for anything. */
    root.setAttribute("data-in-discord", "error");
    console.error("openaliro: Discord SDK handshake failed; the twin runs on regardless:", e);
  }
}

void boot();
