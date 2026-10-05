#!/bin/sh
# regen-rnds-patch.sh <es src tree> <es-rgds-rnds.patch>: rewrites the patch's sections for the rnds engine's own files
# (new files in that patch) from the tree's current copies; its hunks in ES's files (CMakeLists, GuiMenu, main,
# SystemView, ViewController, ThemeData) stay as they are. The tree is the one tools/build-es.sh patched (the engine
# files are untracked there), after editing the engine.
set -e
SRC=$1; PATCH=$2
FILES="es-app/src/rnds/RndsEs.cpp es-app/src/rnds/RndsEs.h es-app/src/rnds/RndsRaster.cpp es-app/src/rnds/RndsRaster.h
       es-app/src/rnds/RndsUI.cpp es-app/src/rnds/RndsUI.h es-app/src/views/gamelist/RndsGameListView.cpp
       es-app/src/views/gamelist/RndsGameListView.h"
cd "$SRC"
git add -N $FILES
git diff -- $FILES > "$PATCH.new-files"
git reset -q -- $FILES
python3 - "$PATCH" "$PATCH.new-files" <<'PY'
import re, sys
patch, fresh = sys.argv[1], sys.argv[2]
def sections(text):
    parts = re.split(r'(?m)^(?=diff --git )', text)
    head, rest = parts[0], parts[1:]
    return head, {re.match(r'diff --git a/(\S+)', s).group(1): s for s in rest}
head, old = sections(open(patch).read())
_, new = sections(open(fresh).read())
missing = [p for p in new if p not in old]
assert not missing, 'not in the patch: %s' % missing
out = head + ''.join(new.get(p, old[p]) for p in old)
open(patch, 'w').write(out)
print('%s: %d sections, %d regenerated' % (patch, len(old), len(new)))
PY
rm -f "$PATCH.new-files"
