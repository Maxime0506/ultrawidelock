# mk/web.mk — the site: landing page, guides, flasher, twin, subsystem graph.
#
# This is not the `make docs` that was removed. That one shelled out to a page
# generator living outside the repository, wrote a page per source file into
# docs/, and committed all 476 of them; every merge then conflicted on derived
# line numbers, which is why it needed a `make sync` whose own comment admitted
# only a regeneration could fix them.
#
# Nothing here is committed. web/dist/ is gitignored and disposable, the
# generator is stdlib Python in this repository, and the whole site builds with
# no toolchain, no node and nothing installed.
#
# Four optional inputs, each degrading rather than failing:
#   emcc                                  the twin's simulator
#   graphify-out/graph.json               file-level graph data; without it the
#                                         page still builds, from the committed
#                                         web/graph/subsystems.json
#   web/vendor/3d-force-graph.min.js      3D rather than the flat SVG graph
#                                         (needs the graphify data too)
#   build/esp32-matter-lock-*/*.bin       the flasher's images; without them
#                                         the page says so instead of offering
#                                         a button that downloads nothing

.PHONY: docs docs-check docs-serve docs-clean release-all

##@ Docs
## docs: build the website  ->  web/dist/index.html
##   Landing page, the guides under docs/, the flasher, the twin and the graph.
docs:
	@python3 $(REPO_ROOT)/web/build.py

## docs-check: build the website and fail on any dead internal link
docs-check:
	@python3 $(REPO_ROOT)/web/build.py --check

## docs-serve: build, then serve it on http://localhost:8080  ·  DOCS_PORT= to change
##   file:// cannot work: the fonts, the twin's WASM and the 3D renderer are
##   all fetched, so they need an origin. Ctrl-C to stop.
DOCS_PORT ?= 8080

docs-serve: docs
	@# Say who holds the port. http.server's own failure is a 25-line traceback
	@# ending in "Address already in use", which does not name the process and
	@# reads like the build broke.
	@if lsof -nP -iTCP:$(DOCS_PORT) -sTCP:LISTEN >/dev/null 2>&1; then \
	  printf '  port %s is already serving:\n' '$(DOCS_PORT)' >&2; \
	  lsof -nP -iTCP:$(DOCS_PORT) -sTCP:LISTEN \
	    | awk 'NR>1 {printf "    pid %s  %s\n", $$2, $$1}' >&2; \
	  printf '  stop it, or pick another: make docs-serve DOCS_PORT=8081\n' >&2; \
	  exit 1; \
	fi
	@printf '  serving web/dist on http://localhost:%s  (Ctrl-C to stop)\n' '$(DOCS_PORT)'
	@cd $(REPO_ROOT)/web/dist && python3 -m http.server $(DOCS_PORT)

## docs-clean: remove the built site
docs-clean:
	@rm -rf $(REPO_ROOT)/web/dist
	@printf '  removed web/dist\n'

## release-all: build every publishable image, then the site around them
##   The three release bundles plus a site whose flasher actually carries
##   firmware. Building and publishing a release stays entirely local: run it,
##   check the bundles under build/release/, push, then attach them with
##   `gh release create vN.N.N build/release/*/*.bin ...`.
##
##   The one exception is the website. .github/workflows/pages.yml renders and
##   deploys it, because publishing to Pages is the only step a laptop cannot
##   do. It is workflow_dispatch only -- nothing here happens on a push -- and
##   it fetches the firmware above from the latest release rather than building
##   any, so the site is only as current as the last release plus the last time
##   someone ran it.
##
##   Needs both toolchains and RELEASE_KEY=<path> for the signed Zephyr image.
##   Deliberately not the dev key `make dfu-key` writes.
release-all:
	@if [ -z '$(RELEASE_KEY)' ]; then \
	  printf '  release-all needs RELEASE_KEY=<path to the production key>\n' >&2; \
	  printf '  the checkout dev key must not sign a published image.\n' >&2; \
	  exit 1; \
	fi
	@$(MAKE) --no-print-directory esp-release
	@$(MAKE) --no-print-directory release RELEASE_KEY='$(RELEASE_KEY)'
	@$(MAKE) --no-print-directory nrf-release
	@$(MAKE) --no-print-directory docs-check
	@printf '\n  bundles in %s/release, site in web/dist\n' '$(ULTRAWIDELOCK_BUILD_ROOT)'
