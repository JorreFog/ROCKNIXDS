#!/bin/sh
# Builds ROCKNIXDS Bank & Trade for the handheld: dist/rocknixds-bank-<version>-aarch64.tar.gz (and its .sha256).
#
#   sh bank/build.sh            # needs the .NET 10 SDK; downloads the arm64 runtime pack and PKHeX's sprites
#
# The app is self-contained (the .NET runtime is in the package, nothing to install on ROCKNIX), compiled ahead of time
# (ReadyToRun) so it starts quickly on the A55 cores, and the runtime's unused parts are trimmed. PKHeX.Core is kept
# whole: parts of it are reached by reflection. Env: RID=linux-x64 builds for a PC instead.
set -e

HERE=$(cd "$(dirname "$0")" && pwd)
VER=$(cat "$HERE/VERSION")
RID=${RID:-linux-arm64}
ARCH=$(case $RID in linux-arm64) echo aarch64 ;; linux-x64) echo x86_64 ;; *) echo "$RID" ;; esac)
OUT=$HERE/dist
STAGE=$OUT/stage/rocknixds-bank

rm -rf "$OUT/stage"
mkdir -p "$STAGE"
sh "$HERE/tools/fetch-assets.sh" "$HERE/assets"

dotnet publish "$HERE/src/Bank.App/Bank.App.csproj" -c Release -r "$RID" --self-contained \
    -p:PublishReadyToRun=true -p:PublishTrimmed=true -p:TrimMode=partial -p:DebugType=none \
    -p:SatelliteResourceLanguages=en -o "$STAGE"

cp -r "$HERE/assets" "$STAGE/assets"
cp "$HERE/device/rocknixds-bank.sh" "$HERE/device/ROCKNIXDS Bank.sh" "$STAGE/"
chmod +x "$STAGE/rocknixds-bank" "$STAGE/rocknixds-bank.sh" "$STAGE/ROCKNIXDS Bank.sh"
cp "$HERE/LICENSE" "$HERE/THIRD_PARTY.md" "$HERE/VERSION" "$STAGE/"

TAR="$OUT/rocknixds-bank-$VER-$ARCH.tar.gz"
tar -C "$OUT/stage" -czf "$TAR" rocknixds-bank
(cd "$OUT" && sha256sum "$(basename "$TAR")" > "$(basename "$TAR").sha256")
rm -rf "$OUT/stage"
echo "$TAR ($(du -h "$TAR" | cut -f1))"
