.PHONY: tag

# make tag VERSION=YYYY.MM.PATCH: [Unreleased] becomes the release notes, then tag and push.
tag:
	@echo "$(VERSION)" | grep -Eq '^[0-9]{4}\.[0-9]{1,2}\.[0-9]+$$' || { echo "usage: make tag VERSION=YYYY.MM.PATCH"; exit 1; }
	@git diff --quiet HEAD || { echo "commit your changes first"; exit 1; }
	@! git rev-parse -q --verify "refs/tags/v$(VERSION)" >/dev/null || { echo "v$(VERSION) is already tagged"; exit 1; }
	@notes=$$(awk '/^## \[Unreleased\]/ {on=1; next} /^## \[/ {exit} on && (NF || seen) {seen=1; print}' CHANGELOG.md); \
	[ -n "$$notes" ] || { echo "nothing under [Unreleased] in CHANGELOG.md"; exit 1; }; \
	head=$$(sed '/^## \[Unreleased\]/,$$d' CHANGELOG.md); \
	older=$$(awk '/^## \[Unreleased\]/ {u=1; next} u && /^## \[/ {on=1} on' CHANGELOG.md); \
	{ printf '%s\n\n## [Unreleased]\n\n## [%s] - %s\n%s\n' "$$head" "$(VERSION)" "$$(date +%F)" "$$notes"; \
	  [ -z "$$older" ] || printf '\n%s\n' "$$older"; } > CHANGELOG.md; \
	sed -i 's/^project(pkgtop VERSION [0-9.]*/project(pkgtop VERSION $(VERSION)/' CMakeLists.txt; \
	git commit -qam "chore(release): v$(VERSION)"; \
	git tag -a "v$(VERSION)" -m "pkgtop $(VERSION)"; \
	git push origin HEAD "v$(VERSION)"
