# mk/docs.mk — the documentation site. Needs doxygen and graphviz; no NCS
# toolchain and no hardware.
#
# Output goes to site/, which is publishable rather than intermediate and so is
# deliberately NOT under the build root.

.PHONY: docs docs-publish

##@ Docs
## docs: build the documentation site  ->  site/index.html
##   Subsystem tree + guides + search, then the Doxygen reference under site/api/,
##   then a link pass that fails the build on any dead link.
docs:
	@$(REPO_ROOT)/scripts/docs.sh

## docs-publish: rebuild the site, then snapshot it onto the local gh-pages branch
##   Never pushes: publishing stays `git push origin gh-pages`. Refuses a stale
##   or partial site, uncommitted docs/, or a foreign branch named gh-pages.
docs-publish: docs
	@$(REPO_ROOT)/scripts/docs-publish.sh
