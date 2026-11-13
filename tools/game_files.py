#!/usr/bin/env python3
"""The list of version 1.4.0's game files for Verify game files (runtime/verify.c): one line per file of a game
folder's exefs/ and romfs/, with its CRC-32 (zlib's), its size and its path. Sizes and hashes only: nothing of the
game's goes in it. Made once from a copy known to be right (the base game with the 1.4.0 update applied, untouched):

    python tools/game_files.py <game folder> android/app/src/main/assets/game_files.txt

The APK carries it in its assets and the PC zip beside MHGU.exe (native/build_windows.sh).
"""
import os
import sys
import zlib

HEADER = ("# MHGU 1.4.0: the game folder's files as version 1.4.0 has them (base game + 1.4.0 update), for Verify game files\n"
          "# (runtime/verify.c). CRC-32, size, path. Hashes and sizes only, no game data. Made by tools/game_files.py.\n")


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    root, out = argv[1], argv[2]
    main_nso = os.path.join(root, "exefs", "main")
    with open(main_nso, "rb") as f:
        head = f.read(0x48)
    if head[:4] != b"NSO0" or head[0x40:0x48] != bytes.fromhex("fb08f1d20fd1204f"):
        print("exefs/main is not version 1.4.0's", file=sys.stderr)
        return 1
    rows = []
    for top in ("exefs", "romfs"):
        for dirpath, dirs, files in os.walk(os.path.join(root, top)):
            dirs.sort()
            for name in sorted(files):
                path = os.path.join(dirpath, name)
                crc, size = 0, 0
                with open(path, "rb") as f:
                    while chunk := f.read(1 << 22):
                        crc, size = zlib.crc32(chunk, crc), size + len(chunk)
                rows.append(f"{crc:08x}\t{size}\t{os.path.relpath(path, root).replace(os.sep, '/')}\n")
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(HEADER)
        f.writelines(rows)
    print(f"{len(rows)} files")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
