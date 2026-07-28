#!/usr/bin/env python3
"""Stage the site: a cinematic landing hero, and a reveal layer everywhere.

The page generator lays every page out the same way — a tinted band with a
title in it, then an article. That is correct and completely flat, and the
landing page in particular arrives looking like page 240 of the reference
tree rather than the front of anything. This pass adds the theatre, entirely
through injections into the rendered output; the generator is never edited.

Three things go in:

  * The landing hero becomes a dark room. `.hero-band` picks up a second
    class that redefines the theme's own custom properties inside it, so
    every child — buttons, the command chip, the terminal card, the stat
    row — restyles itself for a dark surface without a single one of them
    being touched by name. Behind the type, the concentric SVG the generator
    already draws in the corner is animated into UWB ranging pulses (this is
    a proximity-unlock reader; the rings are the product), over a drifting
    terracotta glow and a fine grain. The wordmark goes up to ~5.5rem of
    serif. The terminal tilts a few degrees out of the page.
  * The Get-started page gets the same room a size down — same glow, grain
    and pulses, a shorter band, no terminal or stat row. It is the only
    other page that is a front door rather than a document, and arriving
    there from the landing page should not feel like leaving the site. Its
    rings are injected here, because the page the pass builds it from has
    none of its own.
  * The explore cards become a bento: three columns, with the first and last
    cell double-width, and the first promoted to a display card. Every card
    tracks the pointer with a soft spotlight, as do the Get-started tracks.
  * Sitewide, section headings grow from 11px uppercase rails into serif
    headings, structural blocks fade up as they enter the viewport, the
    numbers in the hero count up once, and a hairline progress bar tracks
    reading position.

All of it is behind `prefers-reduced-motion`: the reveal layer resolves to
"already visible", the counters print their final value, the pulses and the
drift stop, and the terminal sits flat. The script is inert on a page with
none of these hooks, so guides and reference pages take only the heading and
reveal treatment.

Idempotent: a marker guards each injection, so re-running over a kept site/
changes nothing. Run from the repo root after docs_theme.py (it builds on the
tokens that pass defines) and before the link pass; it adds no links.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

SITE = Path("site")
INDEX = SITE / "index.html"
START = SITE / "start.html"

MARK = "gv-stage"
NAV_ANCHOR = '<script defer src="nav.js"></script>'
HERO_OPEN = '<header class="hero-band">'
HERO_IN = '<div class="hero-in">'

# The rings the generator draws on the landing page, repeated for the page
# docs_start.py builds — it assembles that one from a guide, and guides have
# no corner art. Same geometry, so one keyframe animation drives both.
HERO_ART = (
    '<svg class="hero-art" viewBox="0 0 400 400" aria-hidden="true" fill="none" '
    'stroke="currentColor">'
    '<circle cx="330" cy="70" r="40" stroke-width="1"/>'
    '<circle cx="330" cy="70" r="95" stroke-width="1"/>'
    '<circle cx="330" cy="70" r="155" stroke-width="1"/>'
    '<circle cx="330" cy="70" r="220" stroke-width="1"/>'
    '<circle cx="330" cy="70" r="290" stroke-width="1"/>'
    '<circle cx="330" cy="70" r="4" fill="currentColor" stroke="none"/>'
    "</svg>"
)

# The grain is a two-octave turbulence rendered once into a data URI: cheaper
# than a PNG, and it scales with the band instead of tiling visibly.
GRAIN = (
    "url(\"data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' "
    "width='140' height='140'%3E%3Cfilter id='n'%3E%3CfeTurbulence "
    "type='fractalNoise' baseFrequency='.85' numOctaves='2'/%3E%3C/filter%3E"
    "%3Crect width='140' height='140' filter='url(%23n)' opacity='.5'/%3E"
    "%3C/svg%3E\")"
)

# ---------------------------------------------------------------------------
# The dark room. Redefining the tokens on .hero-cine is what makes every
# child follow: the base sheet paints .btn, .cmdchip, .term and .stat-b out
# of --surface/--line/--ink, so a scoped redefinition restyles all of them at
# once. Contrast against the band's own #171614: --ink 12.3:1, --muted 7.3:1,
# --faint 5.3:1, --accent 6.6:1, and the primary button's label 5.8:1.
# ---------------------------------------------------------------------------
HERO_CSS = """<style id="gv-stage-hero">
.hero-cine{
  --ground:#171614; --surface:#1f1e1c; --raise:#2a2825; --card:#1f1e1c;
  --ink:#d8d5cb; --strong:#faf9f5; --muted:#a8a59b; --faint:#8e8b82;
  --line:rgba(250,249,245,.14); --hairline:rgba(250,249,245,.08);
  --accent:#e0855f; --accent-ink:#e8a883;
  --tint:rgba(224,133,95,.14); --tint-line:rgba(224,133,95,.34);
  --btn:#d97757; --btn-hover:#e08c6d; --btn-ink:#171614;
  /* the dark step of --ok, because the page's light one is 1.9:1 in here */
  --ok:#8ac9a1;
  position:relative;isolation:isolate;background:#171614;color:var(--ink);
  border-bottom:1px solid rgba(250,249,245,.1)}
.hero-cine .hero-in{position:relative;z-index:2;padding-top:4.6rem;padding-bottom:4.2rem}

/* Backdrop: a drifting glow behind the type, the generator's own corner
   rings turned into range pulses, and grain over the top of both. */
.hero-fx{position:absolute;inset:0;z-index:0;overflow:hidden;pointer-events:none}
.hf-glow{position:absolute;inset:-40% -20%;
  background:radial-gradient(46% 44% at 76% 14%,rgba(217,119,87,.38),transparent 68%),
    radial-gradient(38% 40% at 12% 96%,rgba(201,100,66,.20),transparent 70%);
  animation:hf-drift 22s ease-in-out infinite alternate}
.hf-grain{position:absolute;inset:0;opacity:.05;background-image:__GRAIN__;
  background-size:140px 140px}
@keyframes hf-drift{from{transform:translate3d(-2%,-1%,0) scale(1)}
  to{transform:translate3d(3%,2%,0) scale(1.12)}}

.hero-cine .hero-art{z-index:1;opacity:1;color:#f0a880;width:44rem;height:44rem;
  right:-11rem;top:-12rem;mix-blend-mode:screen}
.hero-cine .hero-art circle{transform-origin:330px 70px;opacity:0;stroke-width:1.4;
  animation:hf-ping 5.6s cubic-bezier(.16,.7,.25,1) infinite}
.hero-cine .hero-art circle:nth-of-type(2){animation-delay:1.4s}
.hero-cine .hero-art circle:nth-of-type(3){animation-delay:2.8s}
.hero-cine .hero-art circle:nth-of-type(4){animation-delay:4.2s}
@keyframes hf-ping{0%{transform:scale(.14);opacity:0}
  10%{opacity:.85}60%{opacity:.3}100%{transform:scale(1.6);opacity:0}}

/* Type: the wordmark is the whole picture, so it gets the room. */
.hero-cine .hero h1{font-size:clamp(3.1rem,7.6vw,5.6rem);font-weight:400;
  line-height:.98;letter-spacing:-.022em;color:var(--strong);
  text-shadow:0 0 70px rgba(224,133,95,.3);margin:.5rem 0 .9rem}
.hero-cine .hero .lede{font-size:1.24rem;line-height:1.55;max-width:35rem;color:var(--muted)}
.hero-cine .hero .pill{color:var(--accent-ink)}
.hero-cine .stat-b b{font-size:2.25rem;letter-spacing:-.03em;color:var(--strong)}
.hero-cine .stat-b span{color:var(--faint)}
.hero-cine .stats{margin-top:2.4rem}

/* Repainting the controls by name rather than leaving them to inherit the
   tokens above. The redefinition does reach them — a button in here reports
   --surface as the band's value — but the declarations that consume it live
   in rules the theme layer already re-declares at equal specificity, and the
   band cannot win that from an ancestor. Naming them here settles it, and
   costs one rule each. */
.hero-cine .btn{background:var(--surface);border-color:var(--line);
  color:var(--ink);backdrop-filter:blur(6px)}
.hero-cine .btn:hover{border-color:var(--tint-line);color:var(--accent-ink)}
.hero-cine .btn-primary,.hero-cine .btn-primary:hover{color:var(--btn-ink)}
.hero-cine .btn-primary{background:var(--btn);border-color:var(--btn)}
.hero-cine .btn-primary:hover{background:var(--btn-hover);border-color:var(--btn-hover)}
.hero-cine .cmdchip{background:rgba(250,249,245,.05);border-color:var(--line);
  color:var(--ink)}
.hero-cine .cmdchip .t-p{color:var(--accent-ink)}
.hero-cine .cmdchip button{background:rgba(250,249,245,.07);
  border-color:var(--line);color:var(--muted)}
.hero-cine .cmdchip button:hover{color:var(--accent-ink);border-color:var(--tint-line)}

/* The terminal leaves the page by a few degrees, and rights itself when you
   go to read it. */
.hero-cine .term{transform:perspective(1400px) rotateY(-7deg) rotateX(2.5deg) scale(1.02);
  transform-origin:left center;box-shadow:0 50px 100px -34px rgba(0,0,0,.9),
    0 0 0 1px rgba(250,249,245,.07),0 0 90px -40px rgba(224,133,95,.5);
  transition:transform .5s cubic-bezier(.2,.7,.2,1)}
.hero-cine .term:hover{transform:perspective(1400px) rotateY(0) rotateX(0) scale(1.02)}
@media (max-width:1099px){.hero-cine .term{transform:none}
  .hero-cine .term:hover{transform:none}}
/* On a phone the band is narrow enough that a pulse crosses the lede rather
   than passing behind the corner, so it pulls in and drops back. */
@media (max-width:900px){.hero-cine .hero-art{width:30rem;height:30rem;
  right:-10rem;top:-8rem;opacity:.5}
  .hero-cine .hero-in{padding-top:3.2rem;padding-bottom:3rem}}

/* The Get-started band: the same room one size down. It carries a title, a
   lede and three facts, so it needs less height than the landing page and a
   wordmark that does not compete with it. */
.hero-lite .hero-in{padding-top:3.7rem;padding-bottom:3.3rem}
.hero-lite h1{font-size:clamp(2.7rem,5.6vw,4.1rem);font-weight:400;
  line-height:1.02;letter-spacing:-.018em;color:var(--strong);
  text-shadow:0 0 60px rgba(224,133,95,.26);margin:.45rem 0 .8rem}
.hero-lite .lede{font-size:1.14rem;line-height:1.55;max-width:32rem;color:var(--muted)}
/* Lower and further out than the landing page's: this band is half the height,
   so the same offset would put the pulses' origin above it and leave only the
   tail of one arc showing. */
.hero-lite .hero-art{width:34rem;height:34rem;right:-10rem;top:-1.5rem}
.hero-lite .start-meta span{color:var(--ink);border-color:var(--line);
  background:rgba(250,249,245,.045);backdrop-filter:blur(6px)}
@media (max-width:900px){.hero-lite .hero-art{width:25rem;height:25rem;
  right:-9rem;top:-1rem;opacity:.5}
  .hero-lite .hero-in{padding-top:2.8rem;padding-bottom:2.6rem}}

@media (prefers-reduced-motion:reduce){
  .hf-glow{animation:none}
  .hero-cine .hero-art circle{animation:none;opacity:.22}
  .hero-cine .term{transform:none}}
</style>"""

# hf-spot is empty until docs_motion.py's script gives it a pointer to follow.
HERO_FX = (
    '<div class="hero-fx" aria-hidden="true"><span class="hf-glow"></span>'
    '<span class="hf-spot"></span><span class="hf-grain"></span></div>'
)

# ---------------------------------------------------------------------------
# Bento + the sitewide layer. Four explore cards land as 2+1 / 1+2, which
# fills three columns exactly and gives the first card the space to become a
# display card; a fifth would simply flow on below.
# ---------------------------------------------------------------------------
STAGE_CSS = """<style id="gv-stage-css">
/* Section headings: from an 11px uppercase rail to something that actually
   divides the page. */
.doc .section-h{align-items:baseline;gap:1.1rem;margin:4.4rem 0 1.5rem}
.doc .section-h h2{font-family:var(--serif);font-size:1.95rem;font-weight:450;
  letter-spacing:0;text-transform:none;color:var(--strong);line-height:1.15}
.doc .section-h .rule{height:1px;
  background:linear-gradient(90deg,var(--tint-line),transparent)}

/* Bento explore cards, with a spotlight that tracks the pointer. */
@media (min-width:900px){
  .feats{grid-template-columns:repeat(3,minmax(0,1fr));gap:1rem}
  .feats li:first-child,.feats li:nth-child(4){grid-column:span 2}
  .feats li:first-child a{display:flex;flex-direction:column;
    justify-content:flex-end;min-height:13rem;padding:1.6rem 1.7rem}
  .feats li:first-child .f-ico{width:3rem;height:3rem;margin-bottom:1rem}
  .feats li:first-child .f-ico svg{width:1.55rem;height:1.55rem}
  .feats li:first-child .f-name{font-family:var(--serif);font-size:1.75rem;
    font-weight:450;letter-spacing:0}
  .feats li:first-child .f-desc{font-size:.95rem;max-width:34ch;margin-top:.45rem}
}
.feats a{position:relative;overflow:hidden}
.feats a::before{content:"";position:absolute;inset:0;opacity:0;
  transition:opacity .28s ease;pointer-events:none;
  background:radial-gradient(16rem 16rem at var(--mx,50%) var(--my,50%),
    var(--tint),transparent 70%)}
.feats a:hover::before{opacity:1}

/* Reveal layer. The elements start shifted only once the script has said so
   — no JS, no hidden content. */
[data-rv]{opacity:0;transform:translateY(14px);
  transition:opacity .5s cubic-bezier(.2,.7,.2,1),transform .5s cubic-bezier(.2,.7,.2,1);
  transition-delay:calc(var(--rv,0)*70ms)}
[data-rv].rv-in{opacity:1;transform:none}

/* Reading position, as a hairline over the topbar's own border. */
.gv-prog{position:fixed;top:0;left:0;height:2px;width:0;z-index:50;
  background:linear-gradient(90deg,var(--accent),var(--accent-ink));
  transition:width .12s linear}

@media (prefers-reduced-motion:reduce){
  [data-rv]{opacity:1;transform:none;transition:none}
  .gv-prog{transition:none}}
</style>"""

# The script only ever adds classes and custom properties. Anything it cannot
# find, it skips; anything it would animate, it prints outright when the
# reader has asked for less motion.
STAGE_JS = """<script id="gv-stage-js">
(function(){
var calm=matchMedia("(prefers-reduced-motion:reduce)").matches;

function reveal(){
  var sel=".doc .section-h,.doc .feats li,.doc .qs-steps li,.doc .shots,"
    +".doc .twin-cta,.doc .graph-wrap,.doc .paths .path";
  var els=document.querySelectorAll(sel);
  if(!els.length)return;
  if(calm||!("IntersectionObserver" in window)){
    for(var i=0;i<els.length;i++)els[i].classList.add("rv-in");return}
  var io=new IntersectionObserver(function(rows){
    rows.forEach(function(r){
      if(!r.isIntersecting)return;
      r.target.classList.add("rv-in");io.unobserve(r.target)})},
    {rootMargin:"0px 0px -8% 0px",threshold:.08});
  var group=null,n=0;
  for(var j=0;j<els.length;j++){
    var p=els[j].parentNode;
    n=(p===group)?n+1:0;group=p;
    els[j].style.setProperty("--rv",Math.min(n,6));
    els[j].setAttribute("data-rv","");
    io.observe(els[j])}
  /* Failsafe. Hiding content until an observer says otherwise is only safe
     if something always says so: if a browser hands us an observer that
     never fires, every hidden block is shown anyway a few seconds in. */
  setTimeout(function(){
    for(var k=0;k<els.length;k++)els[k].classList.add("rv-in")},4000);
}

/* Count-up. The stat cells read "230", "81%", "1461" — animate the digits
   and keep whatever sits around them. */
function counters(){
  var cells=document.querySelectorAll(".hero-cine .stat-b b");
  if(!cells.length)return;
  var jobs=[];
  for(var i=0;i<cells.length;i++){
    var m=/^(\\D*)(\\d[\\d,]*)(.*)$/.exec(cells[i].textContent.trim());
    if(!m)continue;
    jobs.push({el:cells[i],pre:m[1],to:parseInt(m[2].replace(/,/g,""),10),post:m[3]});
  }
  if(!jobs.length)return;
  if(calm)return;
  jobs.forEach(function(j){j.el.textContent=j.pre+"0"+j.post});
  var t0=null,dur=1100;
  requestAnimationFrame(function step(t){
    if(t0===null)t0=t;
    var k=Math.min((t-t0)/dur,1),e=1-Math.pow(1-k,3);
    jobs.forEach(function(j){
      j.el.textContent=j.pre+Math.round(j.to*e).toLocaleString("en-US")+j.post});
    if(k<1)requestAnimationFrame(step)});
}

/* Pointer spotlight on the explore cards and the Get-started tracks. */
function spotlight(){
  var cards=document.querySelectorAll(".feats a,.paths .path");
  for(var i=0;i<cards.length;i++)(function(c){
    c.addEventListener("pointermove",function(e){
      var r=c.getBoundingClientRect();
      c.style.setProperty("--mx",(e.clientX-r.left)+"px");
      c.style.setProperty("--my",(e.clientY-r.top)+"px")})})(cards[i]);
}

/* Reading position. */
function progress(){
  var doc=document.querySelector(".doc");
  if(!doc)return;
  var bar=document.createElement("div");
  bar.className="gv-prog";document.body.appendChild(bar);
  var tick=false;
  function draw(){
    tick=false;
    var h=document.documentElement.scrollHeight-window.innerHeight;
    bar.style.width=(h>0?Math.min(window.scrollY/h,1)*100:0)+"%"}
  addEventListener("scroll",function(){
    if(tick)return;tick=true;requestAnimationFrame(draw)},{passive:true});
  draw();
}

function go(){reveal();counters();spotlight();progress()}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go();
})();
</script>"""


def darken(page: str, extra: str = "") -> str:
    """Turn a page's hero band into the dark room, and add the band's stylesheet."""
    page = page.replace(
        HERO_OPEN,
        f'<header class="hero-band hero-cine{extra}">' + HERO_FX,
        1,
    )
    return page.replace(
        NAV_ANCHOR, NAV_ANCHOR + HERO_CSS.replace("__GRAIN__", GRAIN), 1
    )


def stage_landing(page: str) -> tuple[str, list[str]]:
    """Turn the landing hero into the dark room; report what was applied."""
    if HERO_OPEN not in page:
        return page, []
    return darken(page), ["cinematic hero"]


def stage_start(page: str) -> tuple[str, list[str]]:
    """Give the Get-started band the same room, plus rings of its own."""
    if HERO_OPEN not in page:
        return page, []
    page = darken(page, " hero-lite")
    return page.replace(HERO_IN, HERO_IN + HERO_ART, 1), ["get-started hero"]


def main() -> int:
    """Inject the landing hero treatment and the sitewide reveal layer into the rendered site; report the pages touched."""
    if not INDEX.is_file():
        print("    no rendered site — nothing to stage")
        return 0

    raw = INDEX.read_text()
    if HERO_OPEN not in raw and 'class="hero-cine"' not in raw:
        print(
            "docs_hero: landing page has no hero band to stage — generator "
            "layout changed?",
            file=sys.stderr,
        )
        return 1

    staged = kept = 0
    notes: list[str] = []
    for p in sorted(SITE.glob("*.html")):
        page = p.read_text()
        if f'id="{MARK}-css"' in page:
            kept += 1
            continue
        if NAV_ANCHOR not in page:
            continue
        if p.name == INDEX.name:
            page, did = stage_landing(page)
            notes += did
        elif p.name == START.name:
            page, did = stage_start(page)
            notes += did
        page = page.replace(NAV_ANCHOR, NAV_ANCHOR + STAGE_CSS + STAGE_JS, 1)
        p.write_text(page)
        staged += 1

    note = f" ({kept} already staged)" if kept else ""
    extra = f", {', '.join(notes)}" if notes else ""
    print(f"    reveal layer on {staged} page(s){note}{extra}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
