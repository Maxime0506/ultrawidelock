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
# Three optional inputs, each degrading rather than failing:
#   emcc                                  the twin's simulator
#   graphify-out/graph.json               the graph page
#   web/vendor/3d-force-graph.min.js      3D rather than the flat SVG graph

.PHONY: docs docs-check docs-serve docs-clean

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
