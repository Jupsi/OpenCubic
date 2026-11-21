#!/usr/bin/env bash
# GUI check inside a ci/ image: build CLI + GUI, run all tests offscreen, then run the
# smoke test and the real app on an X server (xcb, like a desktop session).
# Screenshots go to /shots when it is mounted.
set -euo pipefail
cp -r /src/flashtool /w && cd /w
launcher=(); command -v ccache >/dev/null && launcher=(-DCMAKE_CXX_COMPILER_LAUNCHER=ccache)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_GUI=ON "${launcher[@]}"
cmake --build build
ctest --test-dir build --output-on-failure

distro=$(. /etc/os-release; [ "$ID" = arch ] && echo arch || echo "${ID}-${VERSION_ID}")
shots=/shots; [ -d "$shots" ] || shots=/tmp/shots
export ACEFLASH_SCREENSHOT_DIR="$shots/$distro"
# hand screenshots back to the owner of the source tree (container runs as root)
trap 'chown -R "$(stat -c %u:%g /src)" "$shots" 2>/dev/null || true' EXIT
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
qtver=$(./build/tst_gui -platform offscreen 2>/dev/null | sed -n 's/^Config: Using QtTest library \([0-9.]*\).*/\1/p')
echo "GUI OK ($(. /etc/os-release; echo "$PRETTY_NAME"), Qt ${qtver:-?})"
