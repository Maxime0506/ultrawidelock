/* "N watching": free social presence, no backend.
 *
 * Discord synchronises no state between Activity instances, so each viewer
 * drives their own twin. This strip is the one honest exception: it shows who
 * else has the Activity open, which makes a shared session feel shared without
 * a sync server behind it.
 *
 * Everything here is untrusted input. Discord's own documentation says not to
 * treat what the SDK reports client-side as truth, and a username is a string
 * another person chose. Nothing in this file ever reaches innerHTML.
 */

import type { DiscordSDK } from "@discord/embedded-app-sdk";
import { Events } from "@discord/embedded-app-sdk";

/* Only the fields actually rendered. The payload carries more (avatar,
 * discriminator, premium_type); if it is not read, it cannot be mishandled. */
type Participant = {
  id?: string;
  username?: string;
  global_name?: string | null;
  nickname?: string | null;
};

const MAX_NAMES = 3; /* beyond this the strip becomes a crowd, not a signal */
const MAX_NAME_LEN = 24;

/* Characters that let a display name lie about its own contents: bidi
 * overrides can visually reverse surrounding text, and zero-width characters
 * can pad a name past a length check while rendering as nothing. textContent
 * stops markup injection but not either of these, because neither is markup. */
const DECEPTIVE = /[\x00-\x1F\x7F​-‏‪-‮⁦-⁩﻿]/g;

function cleanName(p: Participant): string {
  const raw = p.nickname || p.global_name || p.username || "";
  const flat = raw.replace(DECEPTIVE, "").replace(/\s+/g, " ").trim();
  if (!flat) return "someone";
  return flat.length > MAX_NAME_LEN ? flat.slice(0, MAX_NAME_LEN - 1) + "…" : flat;
}

function styleOnce(): void {
  if (document.getElementById("oa-watching-style")) return;
  const style = document.createElement("style");
  style.id = "oa-watching-style";
  /* Borrows the twin's own custom properties, so this follows the theme
   * toggle for free and needs no light/dark branch of its own. */
  style.textContent = `
    .oa-watching {
      display: none; align-items: center; gap: .4rem;
      white-space: nowrap; flex: none;
      font-size: .72rem; color: var(--muted);
      border: 1px solid var(--line); border-radius: 99px;
      padding: .2rem .6rem;
    }
    .oa-watching[data-shown] { display: inline-flex; }
    .oa-watching b { font-weight: 650; color: var(--ink); }
    .oa-watching .oa-dot {
      width: .4rem; height: .4rem; border-radius: 99px;
      background: var(--green, currentColor); flex: none;
    }
    @media (max-width: 520px) { .oa-watching .oa-names { display: none; } }
  `;
  document.head.appendChild(style);
}

function mount(): HTMLElement | null {
  const existing = document.querySelector<HTMLElement>(".oa-watching");
  if (existing) return existing;

  const bar = document.querySelector(".topbar");
  if (!bar) return null; /* the twin's layout changed; do nothing rather than guess */

  styleOnce();
  const el = document.createElement("span");
  el.className = "oa-watching";
  el.setAttribute("aria-live", "polite");

  /* Before the theme button so it lands after the flexible .sub and cannot
   * displace the existing lockup. */
  const themeBtn = document.getElementById("themeBtn");
  bar.insertBefore(el, themeBtn);
  return el;
}

function render(el: HTMLElement, participants: Participant[]): void {
  const n = participants.length;
  if (n <= 0) {
    el.removeAttribute("data-shown");
    return;
  }

  const names = participants.slice(0, MAX_NAMES).map(cleanName);
  const rest = n - names.length;
  const summary = rest > 0 ? `${names.join(", ")} +${rest}` : names.join(", ");

  /* Rebuilt from nodes every time. textContent on every user-derived string,
   * so a name containing markup renders as that markup's literal text. */
  el.replaceChildren();

  const dot = document.createElement("span");
  dot.className = "oa-dot";
  el.appendChild(dot);

  const count = document.createElement("b");
  count.textContent = String(n);
  el.appendChild(count);

  const label = document.createElement("span");
  label.textContent = "watching";
  el.appendChild(label);

  const who = document.createElement("span");
  who.className = "oa-names";
  who.textContent = summary;
  el.appendChild(who);

  el.setAttribute("data-shown", "");
}

/* Never throws and never rejects: presence is a nicety, and the twin must not
 * lose so much as a frame to it. */
export async function startParticipants(sdk: DiscordSDK): Promise<void> {
  let el: HTMLElement | null = null;
  try {
    el = mount();
    if (!el) return;

    const update = (data: unknown) => {
      try {
        const list = (data as { participants?: Participant[] })?.participants;
        if (Array.isArray(list) && el) render(el, list);
      } catch {
        /* a malformed payload must not break the page */
      }
    };

    const initial = await sdk.commands.getInstanceConnectedParticipants();
    update(initial);

    await sdk.subscribe(Events.ACTIVITY_INSTANCE_PARTICIPANTS_UPDATE, update);
  } catch (e) {
    /* Most likely cause is that this command needs a scope this Activity
     * deliberately does not request. Losing the strip is the correct outcome;
     * asking for an OAuth scope to render a headcount is not. */
    el?.removeAttribute("data-shown");
    console.warn("openaliro: participant presence unavailable, continuing without it:", e);
  }
}
