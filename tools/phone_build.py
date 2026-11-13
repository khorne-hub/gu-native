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

Everything this writes is made from the player's own game and stays on the phone.
"""
import argparse
import errno
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
