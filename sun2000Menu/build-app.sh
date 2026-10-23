#!/bin/sh
set -eu

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPOSITORY_ROOT=$(CDPATH= cd -- "$APP_DIR/.." && pwd)
APP_PATH=${1:-"$REPOSITORY_ROOT/build/sun2000Menu.app"}
CONTENTS_PATH="$APP_PATH/Contents"

swift build --package-path "$APP_DIR" --configuration release

mkdir -p "$CONTENTS_PATH/MacOS"
cp "$APP_DIR/.build/release/sun2000Menu" "$CONTENTS_PATH/MacOS/sun2000Menu"
cp "$APP_DIR/Resources/Info.plist" "$CONTENTS_PATH/Info.plist"
printf 'App erstellt: %s\n' "$APP_PATH"
