# mk/extras.mk — housekeeping. Included last, so `make help` ends here.

.PHONY: clean ws-clean help

##@ Housekeeping
## clean: remove every build artifact in the tree  ->  ./build and the app-local ones
##   One rm for the shared root, plus the directories ESP-IDF writes when idf.py
##   is called directly inside an app instead of through `make esp-build`.
clean:
	@# ALIRO_BUILD_ROOT is `?=` and exported (Makefile:38-39), so whatever is in
	@# the caller's environment wins -- and this line deletes it recursively. A
	@# stale export from another checkout would aim that delete outside the repo,
	@# so refuse anything that is not a real subdirectory of it. `..` is rejected
	@# separately because a path can start with $(REPO_ROOT) and still climb out.
	@root='$(ALIRO_BUILD_ROOT)'; repo='$(REPO_ROOT)'; \
	case "$$root" in \
	  *..*) printf '  refusing: ALIRO_BUILD_ROOT contains ".." -- %s\n' "$$root" >&2; exit 1;; \
	  "$$repo"/?*) ;; \
	  *) printf '  refusing: ALIRO_BUILD_ROOT is not inside %s -- %s\n' "$$repo" "$$root" >&2; \
	     printf '  It is exported, so a value left in your environment redirects this delete.\n' >&2; \
	     exit 1;; \
	esac; \
	rm -rf "$$root"
	@# The variable is quoted but the globs are not, which is the point: quoting
	@# the whole word would stop `*` expanding.
	@rm -rf "$(REPO_ROOT)"/ports/esp32/apps/*/build "$(REPO_ROOT)"/ports/esp32/apps/*/build-piv \
	        "$(REPO_ROOT)"/ports/esp32/test/on_target_ec/build "$(REPO_ROOT)"/ports/nrf5340dk/on_target_ec/build
	@printf '  removed %s and the app-local build directories\n' '$(ALIRO_BUILD_ROOT)'

## ws-clean: remove THIS worktree's local build + workspace
##   A symlinked workspace (pointing at the primary) is left alone — only a real
##   local dir is removed, never the shared source.
ws-clean: clean
	@if [ -d workspace ] && [ ! -L workspace ]; then rm -rf workspace && printf '  removed ./workspace\n'; \
	else printf '  (no local workspace to remove)\n'; fi

## help: this grouped, colourised target list
help:
	@if [ -t 1 ] && [ -z "$$NO_COLOR" ]; then \
	  b=$$(printf '\033[1m'); c=$$(printf '\033[36m'); y=$$(printf '\033[1;33m'); d=$$(printf '\033[2m'); r=$$(printf '\033[0m'); \
	else b=; c=; y=; d=; r=; fi; \
	printf '\n  %sOpenAliro%s  %s·  Aliro NFC + UWB firmware  ·  bare targets mean the DWM3001CDK%s\n' "$$b" "$$r" "$$d" "$$r"; \
	awk -v c="$$c" -v y="$$y" -v d="$$d" -v r="$$r" \
	  '/^##@ / { printf "\n  %s%s%s\n", y, substr($$0,5), r; next } \
	   /^## [^ ]/ { s=substr($$0,4); i=index(s,": "); \
	     printf "    %s%-18s%s %s%s%s\n", c, substr(s,1,i-1), r, d, substr(s,i+2), r }' \
	  $(MAKEFILE_LIST); \
	printf '\n  %sOptions%s  %s·  set on the command line, e.g. make nrf-build PRETTY=1%s\n' "$$y" "$$r" "$$d" "$$r"; \
	printf '    %sPRISTINE=1  ·  from-scratch build (every port)%s\n' "$$d" "$$r"; \
	printf '    %sLTO=0  RELEASE=1  SMP=1  DFU_LOG=1  ·  DWM3001CDK%s\n' "$$d" "$$r"; \
	printf '    %sCDK_BUILD=<dir>  CDK_RTT_BUILD=<dir>  CDK_KEY=<path>  ·  DWM3001CDK%s\n' "$$d" "$$r"; \
	printf '    %sAPP=matter-lock|reader|initiator  TARGET=esp32s3|esp32c5|esp32c6  VARIANT=presence|hamqtt|piv%s\n' "$$d" "$$r"; \
	printf '    %sCHIP=dw3720  PRETTY=1  SELFTEST=1  STRICT=1  ·  nRF5340 DK%s\n' "$$d" "$$r"; \
	printf '    %sHA=1  ·  Home Assistant variant; set on bootstrap AND nrf-build%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_SOURCE=0  ·  legacy Nordic binary fallback -> build/nrf5340dk-blob%s\n' "$$d" "$$r"; \
	printf '    %sCIR=1  ·  CIA/CIR diagnostics%s\n' "$$d" "$$r"; \
	printf '    %sALIRO_TRACE=1  ·  unavailable: required vendor trace patch is absent%s\n' "$$d" "$$r"; \
	printf '    %sNFC=pn532|st25r|none  ·  reader transport; default st25r%s\n' "$$d" "$$r"; \
	printf '\n  %sMoved%s  %scdk-aliro-matter-thread -> build   cdk-reader -> reader   cdk-rtt -> monitor   term -> nrf-term%s\n' "$$y" "$$r" "$$d" "$$r"; \
	printf '\n'
