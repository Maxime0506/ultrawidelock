# mk/docs.mk — the documentation site. Needs doxygen and graphviz; no NCS
# toolchain and no hardware.
#
# Output goes to site/, which is publishable rather than intermediate and so is
# deliberately NOT under the build root.
#
# Publishing is not done from here. .github/workflows/docs.yml runs `make docs`
# on a push to main and hands site/ to actions/deploy-pages, so the live site is
# built from source by CI rather than snapshotted from a working tree. A local
# `make docs` is for checking the result, not for shipping it.

.PHONY: docs sync

##@ Docs
## docs: build the documentation site  ->  site/index.html
##   Subsystem tree + guides + search, then the Doxygen reference under site/api/,
##   then a link pass that fails the build on any dead link.
docs:
	@$(REPO_ROOT)/scripts/docs.sh

## sync: after a merge, put the generated artifacts back in step
##   Regenerates the committed docs and then the spec index that cites their line
##   numbers, in that order, dropping the parse cache first. Run this whenever a
##   merge touches docs/ -- the generated files conflict on derived lines that no
##   resolution can settle correctly, only a regeneration can.
sync:
	@$(REPO_ROOT)/scripts/docs-sync.sh
