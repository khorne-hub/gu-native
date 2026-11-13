#!/usr/bin/env python3
"""The on-phone builder's tools for the APK (native/build_native.sh runs it): what tools/build_game.py and
tools/phone_build.py read on the phone (the tools, the runtime headers the game code is compiled with, the HLE symbol
list) into android/app/src/main/assets/builder/tools.zip, and the check that the game code they make on a phone has this
APK's GAMELIB_ID.

    python tools/pack_builder_tools.py GAMELIB_ID

No game data is in there. The files are stored in a fixed order with a fixed date, so the same sources give the same zip.
"""
import os
import sys
import tempfile
import zipfile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
ASSETS = os.path.join(ROOT, 'android', 'app', 'src', 'main', 'assets')
FILES = ['runtime/cpu.h', 'runtime/vfp.h', 'runtime/neon_rt.h', 'runtime/gamelib.h', 'runtime/hle/hle_symbols.txt',
         'tools/gamelib_entry.c']


def files():
    names = sorted(f for f in os.listdir(TOOLS) if f.endswith('.py'))
    names = ['tools/' + f for f in names] + FILES
    names += sorted('tools/android_libc/' + f for f in os.listdir(os.path.join(TOOLS, 'android_libc')))
    return names


def main():
    want = sys.argv[1]
    os.makedirs(os.path.join(ASSETS, 'builder'), exist_ok=True)
    out = os.path.join(ASSETS, 'builder', 'tools.zip')
    with zipfile.ZipFile(out + '.part', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in files():
            info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(os.path.join(ROOT, name), 'rb') as f:
                z.writestr(info, f.read())
    os.replace(out + '.part', out)
    with tempfile.TemporaryDirectory() as tmp:  # (the id as the phone computes it, from the zipped files)
        with zipfile.ZipFile(out) as z:
            z.extractall(tmp)
        sys.path.insert(0, os.path.join(tmp, 'tools'))
        import build_game
        got = build_game.gamelib_id(build_game.ID_FLAGS)
        assert os.path.dirname(build_game.__file__) == os.path.join(tmp, 'tools')
    if got != want:
        sys.exit(f'builder tools: their GAMELIB_ID is {got}, the app\'s is {want}')
    print(f'builder tools: {out} ({os.path.getsize(out) >> 10} KB), GAMELIB_ID {got}')


if __name__ == '__main__':
    main()
