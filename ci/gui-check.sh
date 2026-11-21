#!/usr/bin/env bash
# GUI check inside a ci/ image: build CLI + GUI, run all tests offscreen, then run the
# smoke test and the real app on an X server (xcb, like a desktop session).
# Screenshots go to /shots when it is mounted.
set -euo pipefail
cp -r /src/flashtool /w && cd /w
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON
cmake --build build
ctest --test-dir build --output-on-failure

shots=/shots; [ -d "$shots" ] || shots=/tmp/shots
export ACEFLASH_SCREENSHOT_DIR="$shots"
echo "== tst_gui on xcb (Xvfb)"
xvfb-run -a -s "-screen 0 1280x800x24" ./build/tst_gui -platform xcb

echo "== AceFlashTool on xcb (Xvfb), 5 s"
set +e
xvfb-run -a -s "-screen 0 1280x800x24" timeout 5 ./build/AceFlashTool 2>&1 | tee /tmp/app.log
rc=${PIPESTATUS[0]}
set -e
# 124 = still running when timeout killed it = started fine
if [ "$rc" != 124 ]; then echo "AceFlashTool exited early (rc=$rc)"; exit 1; fi
if grep -qiE "could not|failed|error" /tmp/app.log; then echo "Qt reported problems"; exit 1; fi
echo "GUI OK ($(. /etc/os-release; echo "$PRETTY_NAME"), Qt $(qtpaths6 --qt-version 2>/dev/null || echo ?))"
