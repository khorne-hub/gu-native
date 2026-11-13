#!/usr/bin/env python3
"""Makes the game code ON THE PHONE, for the app's builder (Settings, Game folder, "Build on this phone"): the same
tools/build_game.py a PC runs, for the phone's library only, with Zig's C compiler instead of the NDK (--zig-cc) and a
work folder that is kept (--work), so that a run cut short goes on from where it stopped.

    python3 phone_build.py --game FOLDER --work DIR --cc COMPILER [--jobs N] [--expect-id ID] [--clean]

The app runs it with the CPython for Android it downloaded (python.org) and Zig's C compiler (PyPI's ziglang 0.13.0)
through its two small launchers (android/.../builder/toolrun.c): --cc is libzigcc.so in the app's native library folder.

Besides build_game.py's own output it prints lines for the app, each starting with "@@" (the same as MH4U for Android's
builder, mh4n tools/android/phone_build.py):
    @@pid PID                   this process (the app pauses and goes on with the build's processes from it)
    @@id ID                     the game code id these tools give (the app compares it with its own first)
    @@stage NAME                extract, recompile, compile, link, install
    @@progress NAME DONE TOTAL  how far a stage is
    @@error NAME MESSAGE        the stage that failed and why (the last line of a failed run)
    @@done PATH                 the library is in place, with its .id beside it

Resuming: the translation (the analysis of the game's exefs and the C and function lists it writes: minutes of every
core on a phone) is done again only when what it is made from changed: once it is through, the work folder's inputs.txt
keeps a hash of its inputs (the game's exefs/main and exefs/sdk, these tools and the Python that runs them) and a list of
the C it wrote; a run whose inputs give the same hash, with that C still there, goes straight to compiling the files
whose objects are missing (build_game.py keeps an object made from the same C with the same compiler). The library is
the same, byte for byte, as a run from an empty work folder makes. (The same as MH4U's, mh4n tools/android/phone_build.py.)

Everything this writes is made from the player's own game and stays on the phone.
"""
import argparse
import errno
import hashlib
import io
import os
import re
import shutil
import sys
import threading
import time
import traceback

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)

import build_game  # noqa: E402

LIBRARY = 'libmhgugame.so'
WORK_SPACE = 1500 << 20  # (the C it writes, 300 MB, and its objects)


def emit(line):
    sys.__stdout__.write(line + '\n')
    sys.__stdout__.flush()


# ---- the translation, kept between runs (the module's docstring; MH4U's phone_build.py the same)
STAMP = 'inputs.txt'
WHOLE = 128 << 20  # a file up to this size is hashed whole (exefs/main: tens of MB, under a second on a phone)...
END = 1 << 20      # ...a larger one by its size, its date and its two ends


def file_digest(path):
    st = os.stat(path)
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        if st.st_size <= WHOLE:
            for block in iter(lambda: f.read(1 << 20), b''):
                h.update(block)
            return f'{st.st_size} {h.hexdigest()}'
        h.update(f.read(END))
        f.seek(-END, os.SEEK_END)
        h.update(f.read(END))
    return f'{st.st_size} {st.st_mtime_ns} {h.hexdigest()}'


def any_digest(path):
    return file_digest(path) if path and os.path.isfile(path) else 'none'


def tools_digest():
    """The tools as the app packs them (pack_builder_tools.py's list: the tools, the runtime headers, the HLE list)."""
    import pack_builder_tools
    h = hashlib.sha256()
    for name in pack_builder_tools.files():
        h.update(f'{name} {any_digest(os.path.join(build_game.ROOT, name))}\n'.encode('utf-8', 'surrogateescape'))
    return h.hexdigest()


def game_exefs(game):
    """The game folder's exefs as build_game.check_game finds it (the folder, or its data/), or None."""
    for d in (game, os.path.join(game, 'data')):
        if os.path.isfile(os.path.join(d, 'exefs', 'main')):
            return os.path.join(d, 'exefs')
    return None


def stamp_hash(lines):
    return hashlib.sha256('\n'.join(lines).encode('utf-8', 'surrogateescape')).hexdigest()


def written(work):
    """What the translation left for the compile: the files of gen/main and gen/sdk (names and sizes)."""
    h = hashlib.sha256()
    for part in ('main', 'sdk'):
        d = os.path.join(work, 'gen', part)
        names = sorted(os.listdir(d)) if os.path.isdir(d) else []
        if not any(n.endswith('.c') for n in names):
            return 'none'
        for n in names:
            h.update(f'{part}/{n} {os.path.getsize(os.path.join(d, n))}\n'.encode('utf-8', 'surrogateescape'))
    return h.hexdigest()


def read_stamp(work):
    try:
        with open(os.path.join(work, STAMP), encoding='utf-8', errors='replace') as f:
            first = f.readline().split()
        return (first[0], first[1]) if len(first) >= 2 else ('', '')
    except OSError:
        return '', ''


def write_stamp(work, inputs, made, lines):
    path = os.path.join(work, STAMP)
    with open(path + '.part', 'w', encoding='utf-8', errors='replace') as f:
        f.write(f'{inputs} {made}\n' + '\n'.join(lines) + '\n')
    os.replace(path + '.part', path)


def forget_translation(work):
    """The stamp and build_game.py's own notes of a finished translation (gen/<part>.done): the next run translates."""
    for path in [os.path.join(work, STAMP)] + [os.path.join(work, 'gen', p + '.done') for p in ('main', 'sdk')]:
        try:
            os.remove(path)
        except OSError:
            pass


class Stages(io.TextIOBase):
    """build_game.py's output, passed on as it is, with the app's @@ lines for the stages it goes through."""

    def __init__(self):
        super().__init__()
        self.stage = 'extract'
        self.pending = ''
        self.error = ''
        self.parts = {}  # (the translator's parts, sdk and main: their functions add up)
        self.files = 0
        self.in_error = False
        self.on_stage = None

    def writable(self):
        return True

    def write(self, s):
        sys.__stdout__.write(s)
        self.pending += s
        while '\n' in self.pending:
            line, self.pending = self.pending.split('\n', 1)
            self.line(line.strip())
        return len(s)

    def flush(self):
        sys.__stdout__.flush()

    def set(self, stage):
        if stage != self.stage:
            self.stage = stage
            emit(f'@@stage {stage}')
            if self.on_stage:
                self.on_stage(stage)

    def line(self, text):
        if text.startswith('ERROR: '):
            self.error = text[7:]
            self.in_error = True
        elif self.in_error and text:  # (the lines fail() adds after it: the compiler's own words)
            self.error = (self.error + ' ' + text)[:400]
        elif ': translating the game code' in text:
            self.set('recompile')
        elif ': compiling ' in text:
            m = re.search(r'compiling (\d+) files', text)
            if m:
                self.files = int(m.group(1))
            self.set('compile')
        elif text == 'linking...':
            self.set('link')
        m = re.fullmatch(r'(\w+): (\d+) of (\d+) functions', text)
        if m:
            self.parts[m.group(1)] = (int(m.group(2)), int(m.group(3)))
            done = sum(d for d, _ in self.parts.values())
            # (main, the larger part, comes after sdk: until it says its size, about 70,000 functions)
            total = sum(t for _, t in self.parts.values()) + (0 if 'main' in self.parts else 70000)
            emit(f'@@progress recompile {done} {total}')
        elif m := re.fullmatch(r'(\w+): already translated', text):
            self.parts[m.group(1)] = (1, 1)
        m = re.fullmatch(r'(\d+) of (\d+)', text)
        if m and self.stage == 'compile':
            emit(f'@@progress compile {m.group(1)} {m.group(2)}')


class CompileProgress(threading.Thread):
    """While compiling: how many objects there are (build_game.py says it only every 20 files)."""

    def __init__(self, tap, objs_dir):
        super().__init__(daemon=True)
        self.tap, self.objs_dir = tap, objs_dir
        self.stop = threading.Event()

    def run(self):
        last = None
        while not self.stop.wait(3):
            if self.tap.stage != 'compile':
                continue
            try:
                done = sum(1 for f in os.listdir(self.objs_dir) if f.endswith('.o'))
            except OSError:
                continue
            if done != last and self.tap.files:
                emit(f'@@progress compile {min(done, self.tap.files)} {self.tap.files}')
                last = done


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--game', required=True, help='the game folder (exefs and romfs of 1.4.0)')
    ap.add_argument('--work', required=True, help='the work folder (kept between runs to go on where one stopped)')
    ap.add_argument('--cc', required=True, help="the C compiler (the app's libzigcc.so)")
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 2)
    ap.add_argument('--expect-id', default='', help='the id the app wants: anything else stops before the long part')
    ap.add_argument('--clean', action='store_true', help='once the library is in place, delete the work folder')
    args = ap.parse_args(argv)

    t0 = time.time()
    emit(f'@@pid {os.getpid()}')
    lib_id = build_game.gamelib_id(build_game.ID_FLAGS)
    emit(f'@@id {lib_id}')
    if args.expect_id and args.expect_id != lib_id:
        emit(f'@@error tools these tools make game code with id {lib_id}, the app wants {args.expect_id}')
        return 1
    tap = Stages()
    try:
        os.makedirs(args.work, exist_ok=True)
        have = sum(os.path.getsize(os.path.join(r, f)) for r, _, fs in os.walk(args.work) for f in fs)
        free = shutil.disk_usage(args.work).free
        if free + have < WORK_SPACE:
            emit(f'@@error space {(WORK_SPACE - have - free) >> 20} MB more free space needed')
            return 1
        if not os.access(args.game, os.W_OK):
            emit(f'@@error dest can\'t write in {args.game}')
            return 1
        emit('@@stage extract')

        # the translation: kept from the last run if it was made from the same exefs and tools
        exefs = game_exefs(os.path.abspath(args.game))
        lines = [f'tools {lib_id} {tools_digest()}', 'python ' + ' '.join(sys.version.split()), f'game {args.game}',
                 f'main {any_digest(exefs and os.path.join(exefs, "main"))}',
                 f'sdk {any_digest(exefs and os.path.join(exefs, "sdk"))}']
        inputs = stamp_hash(lines)
        kept_inputs, kept_made = read_stamp(args.work)
        reuse = kept_inputs == inputs and kept_made != 'none' and kept_made == written(args.work)
        if reuse:
            print(f'inputs as the last run\'s ({time.time() - t0:.1f} s to check): its translation is kept, on to the compile')
        else:
            if kept_inputs:
                print('the last run\'s translation isn\'t for these inputs (or its C is gone): translating again')
            forget_translation(args.work)

        def on_stage(stage):
            if stage != 'compile':
                return
            if not reuse:  # (the translation is through: what it was made from, and the C it wrote)
                write_stamp(args.work, inputs, written(args.work), lines)
            print(f'compiling from {time.time() - t0:.1f} s after the start')
        tap.on_stage = on_stage
        progress = CompileProgress(tap, os.path.join(args.work, 'obj_android'))
        progress.start()
        sys.stdout = tap
        try:
            build_game.main([args.game, '--android', '--force', '--zig-cc', args.cc, '--work', args.work,
                             '--jobs', str(args.jobs), '--no-pip'])
        finally:
            progress.stop.set()
            sys.stdout = sys.__stdout__
        game = build_game.check_game(os.path.abspath(args.game))
        placed = os.path.join(game, LIBRARY)
        tap.set('install')
        if args.clean:
            shutil.rmtree(args.work, ignore_errors=True)
        emit(f'@@done {placed}')
        return 0
    except SystemExit as e:
        if e.code in (0, None):
            return 0
        message = tap.error or (str(e.code) if not isinstance(e.code, int) else f'stopped with code {e.code}')
        emit(f'@@error {tap.stage} {" ".join(message.split())}')
        return 1
    except OSError as e:
        traceback.print_exc()
        emit(f'@@error {"space" if e.errno == errno.ENOSPC else tap.stage} {e}')
        return 1
    except MemoryError:
        emit(f'@@error memory {tap.stage}')
        return 1
    except Exception as e:  # noqa: BLE001 (any other failure: the app shows it, the log has the traceback)
        traceback.print_exc()
        emit(f'@@error {tap.stage} {type(e).__name__}: {" ".join(str(e).split())}')
        return 1


if __name__ == '__main__':
    t0 = time.time()
    code = main()
    print(f'({time.time() - t0:.0f} s)')
    sys.exit(code)
