# Distributing the Activity: what it cost

**This was done on 2026-08-04.** The app is verified, discovery is enabled, and every
requirement below is satisfied, so the Activity is launchable by anyone rather than by the
owner alone. The identity cost described further down was paid, knowingly; it is written
up here so the decision is on the record rather than inferred later from a green checkmark
in a portal.

Researched 2026-08-04 from Discord's developer support documentation. Treat every figure
here as needing a re-check before acting on it: these are policy pages, not APIs, and
they change without notice.

## The situation before distribution

Kept because it is the state any fork or re-registration starts in, and because the
symptom reads like a bug when it is not.

The Activity works and is deployed, but **only the app owner and developer-team members
can launch it**. Everyone else in a voice channel sees nothing in the activity picker.
That is not a misconfiguration; it is what an undistributed Activity is.

Two ways around that cost nothing. They were the recommendation here before distribution
happened, and they remain the right first answer for anyone standing this up fresh —
neither needs an identity document.

**App Testers.** The Developer Portal has an App Testers section in the left-hand
toolbar. Up to **50** people can be added by Discord username; each accepts an email
invitation and can then launch the Activity themselves. No verification, no ID, no
listing. For a project whose audience is a handful of people who care about Aliro, fifty
is not a limit anyone is going to reach, and this is almost certainly the right answer
rather than distribution.

**Screen-share.** For a walkthrough where one person drives and the others watch and ask
questions, sharing the window is most of the value, and it needs nothing at all.

Untested, and worth two minutes: whether someone who is **not** a tester can *join* an
already-running instance rather than start one. Server members normally see a Join
Activity button on the tile of an ongoing Activity, and joining is a different action
from launching. Discord's documentation does not say whether that is gated for an
undistributed app. If it is not, a group walkthrough needs no testers either.

## What distribution requires

To be listed in the App Directory, and so launchable by anyone:

1. **The app must be verified.**
2. **Verification requires a government-issued photo ID.** The owner of the team that
   owns the app verifies their identity through **Stripe**, Discord's identity
   verification provider. A driver's licence, passport or federal ID. Stripe retains
   that data for three years under its retention policy.
3. **A publicly reachable Privacy Policy and Terms of Service**, linked from the app.
4. Content appropriate for 13+, no age-restricted material.
5. Continued compliance with Discord's Terms of Service, Community Guidelines, Developer
   Terms of Service and Developer Policy.

Once verified, a **Discovery** tab appears in the portal, with a Discovery Settings form
and a Discovery Status page listing which required fields are still missing. A listed app
gets its own public **App Profile Page** in the directory.

Stated review time is **a few days**, varying with queue congestion. That is Discord's
own wording rather than a service level, so treat it as an estimate.

## The part that actually matters here

Distribution requires government-ID verification, and that is **a decision rather than a
formality** for any project not published under a legal name. The ID goes to Stripe rather
than onto the app's public profile, so it is not a case of a legal name appearing next to
the twin. But it does mean:

- a government ID is tied, inside Discord's and Stripe's systems, to the published app,
  and held for three years
- a Privacy Policy and Terms of Service have to exist at public URLs, and such documents
  conventionally name a responsible party
- a public App Profile Page exists and is indexable

Whether that trade is worth making is not a technical question and is explicitly not
being made here.

## The choice is not ID-or-nothing

An earlier draft of this page left open whether a lighter path existed. It does: **App
Testers**, above. Fifty named people can launch an undistributed Activity with no
verification and no ID.

So the App Directory is only worth the identity cost if the goal is strangers finding the
twin by browsing Discord, rather than a specific group being shown it. For this project
that is a real difference, and it makes the directory look like the wrong tool rather
than an expensive one.

What remains genuinely unestablished is the join-versus-launch question noted above.

## If it is never distributed

Nothing is wasted. The Activity is a strictly better artefact than it needed to be:

- the same build deploys as an ordinary web page, and does today
- the standalone page and the Activity cannot drift, because the build fails if they do
- the walk-up, single-step mode, the theme toggle under blocked storage, and mobile touch
  input are all verified in the sandboxed-iframe case

Team members can launch it, everyone else can watch it shared. For the audience this
project actually has, that may simply be enough.
