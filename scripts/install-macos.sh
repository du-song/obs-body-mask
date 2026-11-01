#!/usr/bin/env bash
# Install the obs-body-mask OBS plugin from a GitHub release.
# For macOS on Apple Silicon, OBS Studio 33 or later.
#
# Latest release, in one line:
#   curl -fsSL https://github.com/du-song/obs-body-mask/releases/latest/download/install-obs-body-mask.sh | bash
#
# A specific release:
#   bash install-obs-body-mask.sh --version v0.3
#
# The script downloads obs-body-mask.plugin.zip from the release, checks its
# SHA-256, verifies the bundle signature and copies it into
# ~/Library/Application Support/obs-studio/plugins. It does not use sudo.

set -euo pipefail

REPO="du-song/obs-body-mask"
ASSET="obs-body-mask.plugin.zip"
BUNDLE="obs-body-mask.plugin"
MIN_OBS_MAJOR=33
PLUGINS="$HOME/Library/Application Support/obs-studio/plugins"

say() { printf '==> %s\n' "$*"; }
warn() { printf 'warning: %s\n' "$*" >&2; }
die() {
	printf 'error: %s\n' "$*" >&2
	exit 1
}

usage() {
	cat <<EOF
Usage: install-obs-body-mask.sh [--version TAG]

Installs the obs-body-mask OBS plugin from
https://github.com/$REPO/releases

Options:
  -v, --version TAG   install this release tag (for example v0.3);
                      default: the latest release
  -h, --help          show this help
EOF
}

find_obs_app() {
	local app
	for app in "${OBS_APP:-}" "/Applications/OBS.app" "$HOME/Applications/OBS.app"; do
		if [ -n "$app" ] && [ -f "$app/Contents/Info.plist" ]; then
			printf '%s\n' "$app"
			return 0
		fi
	done
	app="$(mdfind "kMDItemCFBundleIdentifier == 'com.obsproject.obs-studio'" 2>/dev/null | head -n 1 || true)"
	if [ -n "$app" ] && [ -f "$app/Contents/Info.plist" ]; then
		printf '%s\n' "$app"
		return 0
	fi
	return 1
}

main() {
	local version="latest"
	while [ $# -gt 0 ]; do
		case "$1" in
		-v | --version)
			[ $# -ge 2 ] || die "--version needs a tag, for example --version v0.3"
			version="$2"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		*) die "unknown option: $1 (see --help)" ;;
		esac
	done

	# --- Checks ------------------------------------------------------------
	[ "$(uname -s)" = "Darwin" ] || die "this plugin is for macOS only"
	# hw.optional.arm64 is 1 on Apple Silicon, also when this shell runs
	# under Rosetta.
	[ "$(sysctl -n hw.optional.arm64 2>/dev/null || echo 0)" = "1" ] ||
		die "this plugin needs a Mac with Apple Silicon (M-series)"

	local app obs_ver obs_major
	if app="$(find_obs_app)"; then
		obs_ver="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
			"$app/Contents/Info.plist" 2>/dev/null || echo "")"
		obs_major="${obs_ver%%.*}"
		say "Found OBS Studio ${obs_ver:-(unknown version)} at $app"
		case "$obs_major" in
		'' | *[!0-9]*) warn "cannot read the OBS version; continuing" ;;
		*)
			[ "$obs_major" -ge "$MIN_OBS_MAJOR" ] ||
				die "OBS Studio $obs_ver is too old; this plugin needs OBS $MIN_OBS_MAJOR or later. Update OBS, or install v0.1 for older OBS: --version v0.1"
			;;
		esac
	else
		warn "OBS Studio not found; installing the plugin anyway"
	fi

	if pgrep -x OBS >/dev/null 2>&1; then
		die "OBS is running. Quit OBS, then run this script again."
	fi

	# --- Download ----------------------------------------------------------
	local tag="$version"
	if [ "$tag" = "latest" ]; then
		# The /releases/latest page redirects to /releases/tag/<tag>.
		local url
		url="$(curl -fsSLI -o /dev/null -w '%{url_effective}' \
			"https://github.com/$REPO/releases/latest" 2>/dev/null || true)"
		case "$url" in
		*/releases/tag/*) tag="${url##*/}" ;;
		*) die "cannot find the latest release of $REPO (no network?)" ;;
		esac
	fi
	local base="https://github.com/$REPO/releases/download/$tag"

	local tmp
	tmp="$(mktemp -d "${TMPDIR:-/tmp}/obs-body-mask.XXXXXX")"
	# shellcheck disable=SC2064 # expand $tmp now
	trap "rm -rf '$tmp'" EXIT

	say "Downloading obs-body-mask $tag"
	curl -fL --progress-bar -o "$tmp/$ASSET" "$base/$ASSET" ||
		die "download failed: $base/$ASSET"

	if curl -fsSL -o "$tmp/$ASSET.sha256" "$base/$ASSET.sha256" 2>/dev/null; then
		local expected actual
		expected="$(awk '{print $1; exit}' "$tmp/$ASSET.sha256")"
		actual="$(shasum -a 256 "$tmp/$ASSET" | awk '{print $1}')"
		[ "$expected" = "$actual" ] ||
			die "SHA-256 mismatch (expected $expected, got $actual); the download is damaged"
		say "SHA-256 OK"
	else
		warn "release $tag has no $ASSET.sha256; skipping the checksum check"
	fi

	# --- Unpack and verify -------------------------------------------------
	ditto -x -k "$tmp/$ASSET" "$tmp/unpacked" || die "cannot unpack $ASSET"
	local src="$tmp/unpacked/$BUNDLE"
	[ -f "$src/Contents/MacOS/obs-body-mask" ] || die "$ASSET does not contain $BUNDLE"
	# The bundle is ad-hoc signed. Clear the quarantine flag, or Gatekeeper
	# blocks the bundled ONNX Runtime dylib and OBS silently skips the plugin.
	xattr -dr com.apple.quarantine "$src" 2>/dev/null || true
	codesign --verify --deep --strict "$src" 2>/dev/null ||
		die "the bundle signature is not valid"

	# --- Install -----------------------------------------------------------
	local dst="$PLUGINS/$BUNDLE"
	mkdir -p "$PLUGINS" 2>/dev/null || die "cannot create $PLUGINS"
	if [ -w "$PLUGINS" ]; then
		rm -rf "$dst" || die "cannot remove the old $dst"
		cp -R "$src" "$PLUGINS/" || die "cannot copy the plugin to $PLUGINS"
	elif [ -d "$dst" ] && [ -w "$dst" ]; then
		# The plugins folder is not writable (another plugin installer can
		# make it owned by root), but our bundle folder is: replace its
		# contents in place.
		rm -rf "$dst/Contents" || die "cannot remove the old $dst/Contents"
		cp -R "$src/." "$dst/" || die "cannot copy the plugin to $dst"
	else
		die "cannot write to $PLUGINS (owner: $(stat -f %Su "$PLUGINS")). Fix it with:
  sudo chown \"$USER\" \"$PLUGINS\" && chmod u+w \"$PLUGINS\"
then run this script again."
	fi
	xattr -dr com.apple.quarantine "$dst" 2>/dev/null || true
	codesign --verify --deep --strict "$dst" 2>/dev/null ||
		die "the installed bundle signature is not valid: $dst"

	say "Installed obs-body-mask $tag to $dst"
	cat <<'EOF'

Start OBS, then select a source > Filters > + > Body Segmentation Mask.
If the filter does not show, check the OBS log:
  grep -i body-mask ~/Library/Application\ Support/obs-studio/logs/*.txt | tail
EOF
}

main "$@"
