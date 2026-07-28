#!/usr/bin/env python3
"""The motion layer: choreograph the arrival, and make the page answer back.

docs_theme.py sets the surfaces and docs_hero.py builds the rooms. Both are
still, though: the site arrives all at once and then sits there. This pass
adds time and reaction to what those two already drew.

Arrival, in order. The hero's parts rise and unblur on a 60ms beat, the
wordmark a character at a time behind them, the terminal card last and from
further away. Then the terminal types itself out: the same two commands the
page already showed, entered rather than printed, with the caret walking
down the lines. Nothing here is new content — every one of those characters
was in the markup already, and is again the moment the animation ends.

Reaction, everywhere after that. The hero tracks the pointer: a warm spot
follows it across the band, the ranging rings lean away from it, and the
terminal turns to face it. The explore cards tilt under it. On scroll the
band's decoration drifts up behind the type and the topbar lifts off the
page. The "On this page" rail grows a marker that slides between sections
instead of the accent jumping. Copy buttons pop green when they land, the
section rules draw themselves in, and where the browser supports it, one
page cross-fades into the next.

Two rules hold all of it together:

  * Nothing may end up hidden. Every entrance is a CSS animation with
    `both` fill from the stylesheet in `<head>`, so it cannot be left
    half-applied by a script that failed to load, and every JS effect only
    ever sets a custom property or adds a class to something already
    visible. The one exception is the terminal text, which the typist
    blanks — so the typist writes it back on the same tick it starts, and
    only ever runs when it found the text itself.
  * `prefers-reduced-motion: reduce` removes the entrances, the parallax,
    the tilt, the typing and the page transition, and leaves the site as
    docs_hero.py drew it. That is checked in the media query for the CSS
    and at the top of each function for the script, because a class that
    was already added cannot be un-added by a media query.

The CSS is appended to site/style.css rather than injected into the pages:
it is the one stylesheet in `<head>`, and an entrance that starts at
opacity 0 has to be parsed before the thing it hides is painted. The script
goes in with the other passes' scripts at the end of the body.

Idempotent by marker. Run from the repo root after docs_hero.py.
"""

from __future__ import annotations

import sys
from pathlib import Path

SITE = Path("site")
CSS = SITE / "style.css"

MARK = "/* aliro-motion */"
NAV_ANCHOR = '<script defer src="nav.js"></script>'
JS_MARK = 'id="gv-motion"'

# ---------------------------------------------------------------------------
# Arrival. The delays are a 60ms beat with the terminal deliberately off it:
# it comes from further away and takes longer, so it reads as behind the type
# rather than beside it.
# ---------------------------------------------------------------------------
ENTER = """
@keyframes gv-rise{from{opacity:0;transform:translate3d(0,26px,0);filter:blur(9px)}}
@keyframes gv-rise-card{from{opacity:0;transform:translate3d(0,38px,0) scale(.965)}}
@keyframes gv-ch{from{opacity:0;transform:translate3d(0,.42em,0) rotateX(-52deg);
  filter:blur(7px)}}

@media (prefers-reduced-motion:no-preference){
  .hero-cine .hero>*,.hero-lite .hero-in>*:not(.hero-art){
    animation:gv-rise .85s cubic-bezier(.16,.84,.3,1) both}
  .hero-cine .hero>*:nth-child(2){animation-delay:.06s}
  .hero-cine .hero>*:nth-child(3){animation-delay:.12s}
  .hero-cine .hero>*:nth-child(4){animation-delay:.18s}
  .hero-cine .hero>*:nth-child(5){animation-delay:.24s}
  .hero-cine .hero>*:nth-child(6){animation-delay:.30s}
  /* the art is child 1 of the lite band, so its content starts at 2 */
  .hero-lite .hero-in>*:nth-child(3){animation-delay:.06s}
  .hero-lite .hero-in>*:nth-child(4){animation-delay:.12s}
  .hero-lite .hero-in>*:nth-child(5){animation-delay:.20s}
  .hero-cine .hero-side{animation:gv-rise-card 1s cubic-bezier(.16,.84,.3,1) .2s both}

  /* Split wordmark. The script cancels the block entrance when it succeeds,
     so the h1 animates once either way. */
  .hero-cine h1.gv-split{animation:none;opacity:1;filter:none;transform:none;
    perspective:600px}
  .gv-split .ch{display:inline-block;
    animation:gv-ch .75s cubic-bezier(.16,.84,.3,1) both;
    animation-delay:calc(.1s + var(--c,0)*32ms)}
}

/* Printing runs animations in some engines and not others; pin the end state
   so a printed hero is never a blank band. */
@media print{
  .hero-cine .hero>*,.hero-cine .hero-side,.hero-lite .hero-in>*,.gv-split .ch{
    animation:none;opacity:1;filter:none;transform:none}}
"""

# ---------------------------------------------------------------------------
# The hero answers the pointer. --px/--py run -1..1 across the band, --mx/--my
# are the raw pixel position for the spot, and --sy is 0..1 down the band's own
# height. All four default to a still hero, so the script failing to load
# leaves exactly what docs_hero.py drew.
# ---------------------------------------------------------------------------
FIELD = """
.hf-spot{position:absolute;inset:0;opacity:0;transition:opacity .6s ease;
  background:radial-gradient(24rem 20rem at var(--mx,50%) var(--my,50%),
    rgba(224,133,95,.22),transparent 68%)}
.hero-cine.gv-live .hf-spot{opacity:1}

.hero-cine .hero-art{transform:translate3d(calc(var(--px,0)*-20px),
    calc(var(--py,0)*-15px + var(--sy,0)*70px),0);
  transition:transform .5s cubic-bezier(.2,.7,.2,1)}
.hero-cine.gv-live .hero-art{transition-duration:.2s}
.hero-cine .hero-fx{transform:translate3d(0,calc(var(--sy,0)*40px),0)}
.hero-cine .hero-in{opacity:calc(1 - var(--sy,0)*.4)}

/* Beating docs_hero.py's own .hero-cine .term on specificity, because that
   rule is a fixed tilt and this one is the same tilt plus the pointer. */
.hero-cine .hero-side .term{transform:perspective(1400px)
    rotateY(calc(-7deg + var(--px,0)*6deg))
    rotateX(calc(2.5deg + var(--py,0)*-5deg)) scale(1.02)}
.hero-cine .hero-side .term:hover{transform:perspective(1400px)
    rotateY(0) rotateX(0) scale(1.03)}
@media (max-width:1099px){.hero-cine .hero-side .term,
  .hero-cine .hero-side .term:hover{transform:none}}

/* Typing. The generator fades the three lines in on a stagger; when the
   typist takes over they are all present from the start and empty. */
.term .t-body.gv-typed .t-line{opacity:1;animation:none}
.term .t-body.gv-typed .t-cur{margin-left:.1em}
"""

# ---------------------------------------------------------------------------
# Everything below the band.
# ---------------------------------------------------------------------------
PAGE = """
/* Explore cards tilt toward the pointer, on top of the spotlight docs_hero.py
   already gives them. 4.5deg is enough to read as depth and small enough that
   the text stays square. */
.doc .feats a{transform:perspective(900px) rotateX(var(--rx,0deg)) rotateY(var(--ry,0deg));
  transition:transform .45s cubic-bezier(.2,.7,.2,1),border-color .16s ease,
    box-shadow .16s ease}
.doc .feats a.gv-tilt{transition-duration:.12s,.16s,.16s}
.doc .feats a:hover{box-shadow:var(--shadow-hover),inset 0 0 0 1px var(--tint-line)}

/* The rule beside a section heading draws itself in with the heading. Scoped
   to [data-rv] so a page without the script keeps a drawn rule. */
.doc .section-h[data-rv] .rule{transform:scaleX(0);transform-origin:left center;
  transition:transform 1s cubic-bezier(.2,.75,.2,1) .12s}
.doc .section-h[data-rv].rv-in .rule{transform:scaleX(1)}

/* Reading position, with the glow the hairline was missing. */
.gv-prog{box-shadow:0 0 12px var(--tint-line),0 0 3px var(--accent)}

/* The topbar leaves the page once you have. */
.topbar{transition:box-shadow .25s ease,border-color .25s ease}
.topbar.gv-lift{border-bottom-color:var(--line);
  box-shadow:0 12px 30px -24px rgba(0,0,0,.55)}

/* Sidebar entries lean toward their target. */
.doclink{transition:background .16s ease,color .16s ease,
  transform .16s cubic-bezier(.2,.7,.2,1)}
.doclink:hover{transform:translateX(2px)}

/* "On this page": one marker that slides, instead of an accent that jumps.
   The rail's own accent border is given up only once the marker is actually
   in the tree, so a page without the script keeps the border it had. */
.toc-rail nav:has(.gv-tocmark) .toc-link.on{border-left-color:transparent}
.gv-tocmark{position:absolute;left:0;top:0;width:2px;border-radius:2px;
  background:var(--accent);opacity:0;
  transition:transform .34s cubic-bezier(.2,.75,.2,1),height .34s ease,
    opacity .25s ease}
.gv-tocmark.on{opacity:1}

/* Links lift their underline rather than only recolouring it. */
.doc p a,.doc li a,.doc td a{transition:text-decoration-color .2s ease,
  text-underline-offset .2s ease}
.doc p a:hover,.doc li a:hover,.doc td a:hover{text-underline-offset:4px}

/* A light passes over the primary action once on hover. */
.btn-primary{position:relative;overflow:hidden}
.btn-primary::after{content:"";position:absolute;top:0;bottom:0;left:0;width:45%;
  pointer-events:none;transform:translateX(-170%) skewX(-20deg);
  background:linear-gradient(90deg,transparent,rgba(255,255,255,.28),transparent)}
.btn-primary:hover::after{animation:gv-sheen .75s cubic-bezier(.3,.7,.4,1)}
@keyframes gv-sheen{to{transform:translateX(330%) skewX(-20deg)}}

/* A copy that landed should feel like it landed. */
@keyframes gv-pop{40%{transform:scale(1.13)}}
.cmdchip button.done,.hero-cine .cmdchip button.done,.prewrap .copy.done{
  color:var(--ok);border-color:var(--ok);animation:gv-pop .35s cubic-bezier(.2,.8,.3,1.3)}

/* One page into the next, where the engine has it. */
@view-transition{navigation:auto}
::view-transition-old(root){animation:gv-vt-out .16s ease both}
::view-transition-new(root){animation:gv-vt-in .34s cubic-bezier(.2,.75,.2,1) both}
@keyframes gv-vt-out{to{opacity:0}}
@keyframes gv-vt-in{from{opacity:0;transform:translate3d(0,10px,0)}}

@media (prefers-reduced-motion:reduce){
  .hf-spot{display:none}
  .hero-cine .hero-art,.hero-cine .hero-fx{transform:none}
  .hero-cine .hero-in{opacity:1}
  .hero-cine .hero-side .term,.hero-cine .hero-side .term:hover{transform:none}
  .doc .feats a{transform:none;transition:border-color .16s ease,box-shadow .16s ease}
  .doc .section-h[data-rv] .rule{transform:none;transition:none}
  .doclink:hover{transform:none}
  .gv-tocmark{transition:opacity .2s ease}
  .btn-primary:hover::after{animation:none}
  .cmdchip button.done,.hero-cine .cmdchip button.done,.prewrap .copy.done{animation:none}
  ::view-transition-old(root),::view-transition-new(root){animation:none}}
"""

# ---------------------------------------------------------------------------
# The script. Every function returns early on the preference or on a page that
# does not have the thing it animates, so the reference tree pays for a scroll
# listener and nothing else.
# ---------------------------------------------------------------------------
JS = """<script id="gv-motion">
(function(){
var calm=matchMedia("(prefers-reduced-motion:reduce)").matches;
var fine=matchMedia("(hover:hover) and (pointer:fine)").matches;
function rafd(fn){var q=false;return function(){if(q)return;q=true;
  requestAnimationFrame(function(){q=false;fn()})}}

/* The wordmark, a character at a time. Built as nodes rather than markup so
   the title is text whatever it contains, and labelled so it is still one
   word to a screen reader. */
function split(){
  if(calm)return;
  var h=document.querySelector(".hero-cine h1");
  if(!h||h.querySelector(".ch"))return;
  var t=h.textContent,frag=document.createDocumentFragment(),n=0;
  if(!t.trim())return;
  for(var i=0;i<t.length;i++){
    if(t[i]===" "){frag.appendChild(document.createTextNode(" "));continue}
    var s=document.createElement("span");
    s.className="ch";s.style.setProperty("--c",n++);s.textContent=t[i];
    frag.appendChild(s)}
  h.setAttribute("aria-label",t);
  h.textContent="";h.appendChild(frag);h.classList.add("gv-split");
}

/* Pointer and scroll over the band. */
function field(){
  var band=document.querySelector(".hero-cine");
  if(!band)return;
  if(fine&&!calm){
    var mx=0,my=0,px=0,py=0;
    var paint=rafd(function(){
      band.style.setProperty("--mx",mx+"px");
      band.style.setProperty("--my",my+"px");
      band.style.setProperty("--px",px.toFixed(3));
      band.style.setProperty("--py",py.toFixed(3))});
    band.addEventListener("pointermove",function(e){
      var r=band.getBoundingClientRect();
      if(!r.width||!r.height)return;
      mx=e.clientX-r.left;my=e.clientY-r.top;
      px=mx/r.width*2-1;py=my/r.height*2-1;
      band.classList.add("gv-live");paint()});
    band.addEventListener("pointerleave",function(){
      px=py=0;band.classList.remove("gv-live");paint()});
  }
  if(calm)return;
  var draw=rafd(function(){
    var h=band.offsetHeight||1;
    band.style.setProperty("--sy",Math.min(Math.max(window.scrollY,0)/h,1).toFixed(3))});
  addEventListener("scroll",draw,{passive:true});
  draw();
}

/* The explore cards. */
function tilt(){
  if(calm||!fine)return;
  var cards=document.querySelectorAll(".feats a");
  for(var i=0;i<cards.length;i++)(function(c){
    var rx=0,ry=0;
    var paint=rafd(function(){
      c.style.setProperty("--rx",rx.toFixed(2)+"deg");
      c.style.setProperty("--ry",ry.toFixed(2)+"deg")});
    c.addEventListener("pointerenter",function(){c.classList.add("gv-tilt")});
    c.addEventListener("pointermove",function(e){
      var r=c.getBoundingClientRect();
      if(!r.width||!r.height)return;
      ry=((e.clientX-r.left)/r.width*2-1)*4.5;
      rx=((e.clientY-r.top)/r.height*2-1)*-4.5;paint()});
    c.addEventListener("pointerleave",function(){
      c.classList.remove("gv-tilt");rx=ry=0;paint()});
  })(cards[i]);
}

/* The terminal enters its own commands. The text is read out of the page and
   written straight back, one character at a time, so the end state is the
   markup the generator produced. */
function type(){
  if(calm)return;
  var body=document.querySelector(".hero-cine .term .t-body");
  if(!body||body.classList.contains("gv-typed"))return;
  var lines=body.querySelectorAll(".t-line"),jobs=[],i;
  for(i=0;i<lines.length;i++){
    var last=lines[i].lastChild;
    if(last&&last.nodeType===3&&last.textContent.trim())
      jobs.push({node:last,text:last.textContent,line:lines[i]})}
  if(!jobs.length)return;
  var cur=body.querySelector(".t-cur");
  for(i=0;i<jobs.length;i++)jobs[i].node.textContent="";
  body.classList.add("gv-typed");
  if(cur)jobs[0].line.appendChild(cur);
  var j=0,c=0;
  /* If a timer is ever dropped the rest of the command still arrives; the
     fuse is cleared the moment the last character lands, so a finished
     terminal leaves nothing pending. */
  var fuse=setTimeout(finish,9000);
  function finish(){
    clearTimeout(fuse);
    for(var k=0;k<jobs.length;k++)
      if(jobs[k].node.textContent!==jobs[k].text)jobs[k].node.textContent=jobs[k].text;
    if(cur)lines[lines.length-1].appendChild(cur);
  }
  function step(){
    var job=jobs[j];
    job.node.textContent=job.text.slice(0,++c);
    if(c<job.text.length)return setTimeout(step,16+Math.random()*26);
    j++;c=0;
    if(j<jobs.length){
      if(cur)jobs[j].line.appendChild(cur);
      return setTimeout(step,320)}
    finish();
  }
  setTimeout(step,700);
}

/* The rail's marker follows whichever link nav.js has marked. Driven off
   scroll rather than a MutationObserver on the rail: nav.js moves the class
   from its own scroll observer, so this reads it in the same frame, and
   watching for the class instead put a layout read inside the callback that
   caused it. The identity check keeps that read to once per change. */
function tocmark(){
  var first=document.querySelector(".toc-rail .toc-link");
  var nav=first&&first.parentNode;
  if(!nav)return;
  if(getComputedStyle(nav).position==="static")nav.style.position="relative";
  var mark=document.createElement("span");
  mark.className="gv-tocmark";nav.appendChild(mark);
  var last;
  var place=rafd(function(){
    var on=nav.querySelector(".toc-link.on");
    if(on===last)return;
    last=on;
    if(!on){mark.classList.remove("on");return}
    mark.style.height=on.offsetHeight+"px";
    mark.style.transform="translateY("+on.offsetTop+"px)";
    mark.classList.add("on")});
  addEventListener("scroll",place,{passive:true});
  addEventListener("resize",function(){last=undefined;place()},{passive:true});
  place();
}

function topbar(){
  var tb=document.querySelector(".topbar");
  if(!tb)return;
  var d=rafd(function(){tb.classList.toggle("gv-lift",window.scrollY>6)});
  addEventListener("scroll",d,{passive:true});d();
}

function go(){split();field();tilt();type();tocmark();topbar()}
if(document.readyState==="loading")addEventListener("DOMContentLoaded",go);
else go();
})();
</script>"""


def main() -> int:
    """Append the motion stylesheet to the rendered site's shell and wire its script into every generated page; report what was touched."""
    if not CSS.is_file():
        print("    no rendered site — nothing to animate")
        return 0

    sheet = CSS.read_text()
    if MARK in sheet:
        print("    stylesheet already carries the motion layer")
    else:
        CSS.write_text(sheet + "\n" + MARK + "\n" + ENTER + FIELD + PAGE)

    wired = kept = 0
    for p in sorted(SITE.glob("*.html")):
        page = p.read_text()
        if JS_MARK in page:
            kept += 1
            continue
        if NAV_ANCHOR not in page:
            continue
        p.write_text(page.replace(NAV_ANCHOR, NAV_ANCHOR + JS, 1))
        wired += 1

    if not wired and not kept:
        print(
            "docs_motion: no page carried the nav.js anchor — generator "
            "layout changed?",
            file=sys.stderr,
        )
        return 1
    note = f" ({kept} already wired)" if kept else ""
    print(f"    motion layer on {wired} page(s){note}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
