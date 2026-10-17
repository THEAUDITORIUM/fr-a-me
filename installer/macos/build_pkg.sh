#!/usr/bin/env bash
# ==============================================================================
#  fr(a)me — construit l'installeur macOS (.pkg)
#
#  Usage : installer/macos/build_pkg.sh <dossier_artefacts_Release> <version> <sortie.pkg>
#  Ex.   : installer/macos/build_pkg.sh build/Frame_artefacts/Release 1.0.0 dist/frame-1.0.0-macos.pkg
#
#  Le .pkg propose trois composants (cochés par défaut) :
#    • VST3       → /Library/Audio/Plug-Ins/VST3/frame.vst3
#    • Audio Unit → /Library/Audio/Plug-Ins/Components/frame.component
#    • Standalone → /Applications/frame.app
#
#  Signature : si les variables MACOS_APP_IDENTITY ("Developer ID Application: …")
#  et MACOS_INSTALLER_IDENTITY ("Developer ID Installer: …") sont définies, les
#  bundles et le .pkg sont signés avec ces identités. Sinon : signature ad hoc
#  (suffisante pour un usage personnel, voir README).
# ==============================================================================
set -Eeuo pipefail      # -E : le piège ERR s'applique aussi dans les fonctions

# En CI, toute erreur devient une annotation lisible dans le résumé du run.
LOG="$(mktemp)"
report_error () {
    local code=$? line=$1
    if [[ -n "${GITHUB_ACTIONS:-}" ]]; then
        local tailtxt
        tailtxt="$(tail -15 "$LOG" 2>/dev/null | sed 's/%/%25/g' | awk '{printf "%s%%0A", $0}')"
        echo "::error title=build_pkg.sh ligne $line (code $code)::$BASH_COMMAND%0A$tailtxt"
    fi
}
trap 'report_error $LINENO' ERR
exec > >(tee -a "$LOG") 2>&1

ART="${1:?dossier des artefacts Release}"
VERSION="${2:?version}"
OUT="${3:?fichier .pkg de sortie}"

ID_BASE="com.frameaudio.frame"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

APP_ID="${MACOS_APP_IDENTITY:--}"          # "-" = signature ad hoc

sign_bundle () {
    local bundle="$1"
    if [[ "$APP_ID" == "-" ]]; then
        codesign --force --deep --sign - "$bundle"
    else
        codesign --force --deep --options runtime --timestamp --sign "$APP_ID" "$bundle"
    fi
    codesign --verify --deep --strict "$bundle"
}

stage_component () {      # $1 = bundle source, $2 = nom du composant, $3 = dossier d'installation
    local src="$1" name="$2" dest="$3"
    [[ -d "$src" ]] || { echo "introuvable : $src" >&2; exit 1; }

    mkdir -p "$WORK/root-$name"
    cp -R "$src" "$WORK/root-$name/"
    sign_bundle "$WORK/root-$name/$(basename "$src")"

    # Composant non relocalisable : toujours installé à l'emplacement prévu.
    pkgbuild --analyze --root "$WORK/root-$name" "$WORK/$name.plist"
    # (pkgbuild ne reconnaît pas toujours un .vst3 comme bundle : la liste peut
    #  être vide, auquel cas il n'y a rien à rendre non relocalisable.)
    local i=0
    while /usr/libexec/PlistBuddy -c "Print :$i" "$WORK/$name.plist" >/dev/null 2>&1; do
        /usr/libexec/PlistBuddy -c "Set :$i:BundleIsRelocatable false" "$WORK/$name.plist"
        i=$((i + 1))
    done

    pkgbuild --root "$WORK/root-$name" \
             --component-plist "$WORK/$name.plist" \
             --identifier "$ID_BASE.$name" \
             --version "$VERSION" \
             --install-location "$dest" \
             "$WORK/$name.pkg"
}

stage_component "$ART/VST3/frame.vst3"          vst3       "/Library/Audio/Plug-Ins/VST3"
stage_component "$ART/AU/frame.component"       au         "/Library/Audio/Plug-Ins/Components"
stage_component "$ART/Standalone/frame.app"     standalone "/Applications"

# Script post-installation du composant AU : rafraîchit le cache des Audio Units.
mkdir -p "$WORK/scripts"
cat > "$WORK/scripts/postinstall" <<'EOF'
#!/bin/sh
killall -9 AudioComponentRegistrar >/dev/null 2>&1 || true
exit 0
EOF
chmod +x "$WORK/scripts/postinstall"
pkgbuild --nopayload --scripts "$WORK/scripts" --identifier "$ID_BASE.refresh" \
         --version "$VERSION" "$WORK/refresh.pkg"

# Distribution : écran d'accueil + choix des composants.
mkdir -p "$WORK/resources"
cat > "$WORK/resources/welcome.html" <<EOF
<html><body style="font-family: Menlo, monospace; font-size: 12px;">
<h2 style="font-weight: normal; letter-spacing: 0.2em;">fr(a)me ${VERSION}</h2>
<p>spectral harmoniser — vst3 · au · standalone</p>
<p>Le plugin apparaît dans votre DAW sous « fr(a)me audio ▸ fr(a)me ».</p>
</body></html>
EOF

cat > "$WORK/distribution.xml" <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>fr(a)me ${VERSION}</title>
    <welcome file="welcome.html"/>
    <options customize="allow" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <domains enable_anywhere="false" enable_currentUserHome="false" enable_localSystem="true"/>
    <os-version min="11.0"/>
    <choices-outline>
        <line choice="vst3"/>
        <line choice="au"/>
        <line choice="standalone"/>
        <line choice="refresh"/>
    </choices-outline>
    <choice id="vst3" title="VST3" description="Plugin VST3 (Ableton Live, Cubase, Bitwig, Reaper, FL Studio…)">
        <pkg-ref id="${ID_BASE}.vst3"/>
    </choice>
    <choice id="au" title="Audio Unit" description="Plugin AU (Logic Pro, GarageBand, Ableton Live…)">
        <pkg-ref id="${ID_BASE}.au"/>
    </choice>
    <choice id="standalone" title="Standalone" description="Application autonome dans /Applications">
        <pkg-ref id="${ID_BASE}.standalone"/>
    </choice>
    <choice id="refresh" visible="false">
        <pkg-ref id="${ID_BASE}.refresh"/>
    </choice>
    <pkg-ref id="${ID_BASE}.vst3">vst3.pkg</pkg-ref>
    <pkg-ref id="${ID_BASE}.au">au.pkg</pkg-ref>
    <pkg-ref id="${ID_BASE}.standalone">standalone.pkg</pkg-ref>
    <pkg-ref id="${ID_BASE}.refresh">refresh.pkg</pkg-ref>
</installer-gui-script>
EOF

mkdir -p "$(dirname "$OUT")"
SIGN_ARGS=()
if [[ -n "${MACOS_INSTALLER_IDENTITY:-}" ]]; then
    SIGN_ARGS=(--sign "$MACOS_INSTALLER_IDENTITY" --timestamp)
fi

productbuild --distribution "$WORK/distribution.xml" \
             --resources "$WORK/resources" \
             --package-path "$WORK" \
             "${SIGN_ARGS[@]+"${SIGN_ARGS[@]}"}" \
             "$OUT"

echo "installeur créé : $OUT"
pkgutil --payload-files "$OUT" | head -40 || true
