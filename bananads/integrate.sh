#!/bin/sh
# integrate.sh <melonDS or BananaDS source tree>: puts the Banana 2D engine into an emulator built on
# melonDS and makes its software renderer use it.
#
#   1. copies gpu2d/GPU2D_Banana.{h,cpp} and gpu2d/GPU_Banana.{h,cpp} into src/
#   2. applies melonds-core.patch (two friend declarations in src/GPU_Soft.h; the only change the
#      engine needs in melonDS's own files)
#   3. adds the two .cpp files to the core library in src/CMakeLists.txt
#   4. makes the Qt frontend construct BananaRenderer where it constructed SoftRenderer
#      (src/frontend/qt_sdl/EmuThread.cpp); another frontend does the same with one line:
#      nds->SetRenderer(std::make_unique<BananaRenderer>(*nds))
#
# Every step checks its anchors and says what it did; a tree that differs from upstream melonDS gets a
# clear message instead of a silent half-integration. Re-running on an integrated tree is a no-op.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
DST=${1:?usage: integrate.sh <melonDS source tree>}
[ -f "$DST/src/GPU_Soft.h" ] || { echo "$DST is not a melonDS source tree (no src/GPU_Soft.h)"; exit 1; }

# 1. the engine
for f in GPU2D_Banana.h GPU2D_Banana.cpp GPU_Banana.h GPU_Banana.cpp; do
    cp "$HERE/gpu2d/$f" "$DST/src/$f"
done
echo "copied the engine into $DST/src"

# 2. the friend declarations
if grep -q "friend class BananaRenderer2D" "$DST/src/GPU_Soft.h"; then
    echo "src/GPU_Soft.h: already patched"
elif (cd "$DST" && git apply --check "$HERE/melonds-core.patch" 2>/dev/null); then
    (cd "$DST" && git apply "$HERE/melonds-core.patch")
    echo "src/GPU_Soft.h: patched"
else
    python3 - "$DST/src/GPU_Soft.h" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
anchor = "    friend class SoftRenderer3D;\n"
if anchor not in s:
    print("src/GPU_Soft.h: could not find 'friend class SoftRenderer3D;' to add the Banana friends after; add\n"
          "    friend class BananaRenderer2D;\n    friend class BananaRenderer;\nnext to SoftRenderer's other friend declarations")
    sys.exit(1)
s = s.replace(anchor, anchor + "    friend class BananaRenderer2D;\n    friend class BananaRenderer;\n", 1)
open(p, "w").write(s)
print("src/GPU_Soft.h: patched")
EOF
fi

# 3. the sources in the core library
python3 - "$DST/src/CMakeLists.txt" <<'EOF'
import sys, re
p = sys.argv[1]
s = open(p).read()
if "GPU2D_Banana.cpp" in s:
    print("src/CMakeLists.txt: already lists the engine")
    sys.exit(0)
m = re.search(r"^(\s*)GPU2D_Soft\.cpp\s*$", s, re.M)
if not m:
    print("src/CMakeLists.txt: could not find GPU2D_Soft.cpp in the core sources; add GPU2D_Banana.cpp and GPU_Banana.cpp next to it")
    sys.exit(1)
indent = m.group(1)
s = s[:m.end()] + "\n" + indent + "GPU2D_Banana.cpp\n" + indent + "GPU_Banana.cpp" + s[m.end():]
open(p, "w").write(s)
print("src/CMakeLists.txt: added GPU2D_Banana.cpp and GPU_Banana.cpp")
EOF

# 4. the frontend
F="$DST/src/frontend/qt_sdl/EmuThread.cpp"
if [ -f "$F" ]; then
    python3 - "$F" <<'EOF'
import sys
p = sys.argv[1]
s = open(p).read()
if "BananaRenderer" in s:
    print("EmuThread.cpp: already uses BananaRenderer")
    sys.exit(0)
old = "std::make_unique<SoftRenderer>(*nds)"
if old not in s:
    print("EmuThread.cpp: could not find std::make_unique<SoftRenderer>(*nds); replace the software renderer's construction with std::make_unique<BananaRenderer>(*nds)")
    sys.exit(1)
s = s.replace(old, "std::make_unique<BananaRenderer>(*nds)")
inc = '#include "GPU.h"\n'
if inc in s and '#include "GPU_Banana.h"' not in s:
    s = s.replace(inc, inc + '#include "GPU_Banana.h"\n', 1)
elif '#include "GPU_Banana.h"' not in s:
    s = '#include "GPU_Banana.h"\n' + s
open(p, "w").write(s)
print("EmuThread.cpp: the software renderer is BananaRenderer")
EOF
else
    echo "no Qt frontend in $DST: construct the renderer with std::make_unique<BananaRenderer>(*nds) where SoftRenderer was"
fi
echo "done: build $DST as usual"
