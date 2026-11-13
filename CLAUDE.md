# MHGU for Android: project notes

A native Android port of Monster Hunter Generations Ultimate 1.4.0, built on MHGURecomp: the game's 32-bit ARM code is translated
to C, and a host runtime reimplements the Switch's services and its NVN graphics API on Vulkan.

This file has two uses: helping someone set the game up, and continuing the project.

## Rules

- This project never provides game files. Don't obtain, link to or explain how to get ROMs or keys, and don't explain how to dump a
  console. The person brings their own `exefs` and `romfs`.
- Never commit or publish `exefs`, `romfs`, `generated/`, `libmhgugame.so`, `libmhgugame.id`, saves or signing keys. Never put game
  code in an APK or a release: `libmhgugame.so` is made from the game, so each person makes their own with `tools/build_game.py`.
- Keep `LICENSE` (the project's own license from 0.1.7: no redistribution without the author's written permission) and
  `THIRD_PARTY_NOTICES` (MHGURecomp's MIT license and the other libraries' licenses require it).
- Don't lower someone's graphics or audio settings without asking.
- Any new menu, notice or touch control must match the game's style: bronze, gold and wine-dark, with no default Android look.

## Helping someone set it up

Follow `README.md`. In short:

1. Check that a folder holds `exefs/main` and a `romfs` with 35,531 files, from version 1.4.0.
2. Run `python tools/build_game.py <that folder>`. It needs Python 3 on Windows or Linux, installs `lz4` (a Python with no lz4 for
   it, 3.15 at first: the tool's own LZ4 reader, handed to `gen.py` through `PYTHONPATH`, so `nso.py` and `GAMELIB_ID` stay), and downloads NDK r27 to
   `~/.mhgu-tool` if there is none (`--ndk <path>` uses another). It leaves `libmhgugame.so` and `libmhgugame.id` in the folder.
3. Copy the folder to the device, anywhere except `Android/data`. With adb: `adb push <folder> /sdcard/MHGU`.
4. Install the APK from Releases. For Samsung's Game Booster: `adb install -r -i com.mhgu.recomp <apk>`.
5. The person opens the app, grants file access and chooses the folder.

With no PC: steps 1, 3, 4 and 5, and the app offers **Build on this phone** for the missing game code (below, "Making the
game code on the phone"). Its log is `Android/data/com.mhgu.recomp/files/builder_log.txt`.

To diagnose a device: `adb logcat -s MHGU`, or `Android/data/com.mhgu.recomp/files/log.txt`. A player whose game stays black can
still share the log: a long press on the app's icon offers Share log (`res/xml/shortcuts.xml`; `LogShareActivity` is exported for
it, and `adb shell am start -a android.intent.action.VIEW -f 0x1000c000 -n com.mhgu.recomp/.LogShareActivity` fires the same
intent). The log's `[vk] limits` line says what the GPU allows (vertex streams, a stage's buffers and textures...).

## Layout

- `runtime/`: the host runtime. `touch.cpp` touch controls and editor, `settings_panel.cpp` the menu, `nvn/vulkan/` the renderer,
  `hle/` game hooks, `platform/android*.c` Android, `config.c` settings and first-run defaults.
- `tools/`: `gen.py`, `lifter.py`, `neon.py`, `nso.py` translate the game; `build_game.py` is the person's tool.
- `android/`: the app. `GameFolderActivity` is the entry screen, `GameActivity` runs the game (a start that skipped the entry
  screen in that process - Recents brings the game back with its old intent, a controller plugged in - goes to it first:
  `GameFolderActivity.gameStarted`, `SDLActivity.startElsewhere`, which sets nothing of SDL's up), `SaveImportActivity` imports a save
  (it is only applied at the next start, before the game runs, so the game can't save over it). `AppHome` is the app's folder
  (below), `SaveFolder` / `SaveFolderActivity` the save folder, `Notices` the unofficial-build and update notices, `FolderBrowser`
  the game-styled folder browser every screen uses, `Look` the shared colours and the game-styled notice. MH4U for Android has
  the same classes with the same texts: change both.
  `ExtraImportActivity` (Menu Options, Import DLC and event quests) takes a save the player chooses whose Download menu
  content (event and challenge quests, item packs, Palicos) becomes the bonus content's donor, `MHGURecomp/bonus/system`
  (`runtime/services/bonus.c` fills a save that has none at the next start); MH4U's copies its quest files into the extra
  data. `ShortcutActivity` (Menu Options, Shortcut with your image) pins a home-screen shortcut with the player's picture.
  New screens are opened by name from the menu (`ui_request_activity("showX")`: a static method of GameActivity).
- `runtime/hle/rewards.c`: Gameplay, Send all rewards to the box (`[Quest Rewards] SendAllRewards`, off; Alfonso's idea): the
  Quest Rewards menu's Send to the Item Box runs the game's own per-item step for every reward at once. Test only on a
  test save; the log's `[rewards]` line says what was sent and what stayed.
- `native/`: CMake and `build_native.sh`. `deps/`: scripts for SDL3, shaderc and FFmpeg.

## Building

Windows, in Git Bash, with the Android SDK, NDK 27.0.12077973, CMake 3.22.1 and Android Studio's JBR.

1. Put the sources in `deps/src/`: `SDL3-3.4.18`, `ffmpeg` (9.0.2) and `shaderc` (2026.4, with glslang 16.6.0 and SPIRV-Tools and
   SPIRV-Headers 1.4.363.0 in its `third_party`). Then run `bash deps/build_deps.sh arm64-v8a` and `bash deps/build_ffmpeg.sh arm64-v8a`.
2. `bash native/build_native.sh arm64-v8a` builds the runtime. With `MHGU_DEV=1` in front, a test build.
3. `cd android && ./gradlew assembleRelease`. It signs only if `android/keystore.properties` and `mhgu.keystore` exist.

The public APK leaves `libmhgugame.so` out. `-Pwithgame` keeps it for your own tests: never publish that APK.

`GAMELIB_ID` is a hash of the generator and of the headers listed in `tools/build_game.py`, `runtime/gamelib.h` and
`hle_symbols.txt` included. If any of them changes, every player must run the tool again, and the app says so. `runtime/*.c` can change
without that: new hooks use `fnhook_set`, new declarations go in another header. Check the id before releasing.

## Building for Windows

In Git Bash, with llvm-mingw 20241217 (UCRT) and the Android SDK's CMake 3.22.1 and Ninja. No MSYS2 needed.

1. The same sources, plus `Vulkan-Headers` (tag vulkan-sdk-1.4.363.0), in `deps/src/` or a folder named by `DEPS_SRC`. Then
   `bash deps/build_deps.sh win64` and `bash deps/build_ffmpeg.sh win64`; `LLVM_MINGW=<its folder or its bin folder>` if it isn't in the scripts' default place.
2. `bash native/build_windows.sh` builds `MHGU.exe` and `mhguffmpeg.dll`, and leaves them with `SDL3.dll` in `native/build/win64/MHGU`.
3. `python tools/build_game.py <folder with exefs and romfs>`, the players' same command, translates once and leaves both
   `libmhgugame.so` (+ `.id`) and `mhgugame.dll` (+ `mhgugame.id`) in that folder; one already up to date is kept, so a player who made the
   phone's only gets the PC's added. `--android` / `--pc` make one only, `--force` both again. It finds or downloads the NDK and
   llvm-mingw (`--ndk`, `--llvm-mingw`). The Android library it makes is the same, byte for byte, as before the PC part existed.
4. `MHGU.exe` finds the folder by itself (`runtime/gamefolder.c`): the one chosen before (`game_folder.txt` beside the settings), `data`
   beside the executable, or its own folder; else it asks with the system's folder dialog, with the checks and messages of Android's
   `GameFolderActivity` (in English or Spanish), and then loads `mhgugame.dll` from that folder.

- `mhgugame.dll` imports the runtime's functions from the executable by name, so it only loads into `MHGU.exe`: don't rename it. The
  tool needs no copy of it: a first link lists what nothing else provides, and `llvm-dlltool` makes the import library from that. The
  variables among them are reached through clang's `.refptr` pointers and go in as `DATA`; a variable that went in as code would be read
  through a jump thunk.
- The game code is built for x86-64-v2 (SSE4.2: any 64-bit processor from 2009 on): `runtime/gamelib.c` refuses an older one
  with a message. The game has no fused multiply-add, and x86-64-v3 measured no faster; never `-march=native`.
- The renderer's `vk*` are volk's pointers here too, from the Vulkan loader SDL opened for the window (`nvn/vulkan/device.c`).
- The Android app's own screens are `runtime/pc_actions.c` here: Game folder, Import / Export save, Log folder, Add cheats, Add a mod
  use SDL3's file dialogs (they answer on their own thread; `settings_panel_request_view` opens the menu's confirmation, `overlay_notice`
  tells the result), and Licenses is a page of the menu reading `licenses/` beside the executable. An imported save waits in
  `save_import/` beside the settings and is put in place at the next start, before the game runs, the current one copied to
  `save_backups/<date>` beside the save folder. `pick folder|save|export|cheats|mod <path>` in the control file answers a dialog.
- Add a mod takes the mod's folder or its `.zip` (`runtime/unzip.c`, the project's own reader: stored and Deflate, ZIP64; every path
  is checked before anything is written). The Cheats and Mods tabs remove a cheat file (its cheats stop at once: `cheats_drop_file`) or
  a mod. A mod the game loaded at its start is only hidden (`mods/_remove.txt`, read by `mods_installed`) and deleted at the next start
  (`pc_apply_mod_changes`), since the game reads its files while it runs; one added again while in use waits as `mods/.update-<name>`.
  Deleting never goes through a link (a junction, a symbolic link: `is_link`): the link alone goes, and what it points to stays.
- `native/build_windows.sh` also puts `pipelines.list`, `licenses/` (with llvm-mingw's runtime, linked into MHGU.exe) and
  `extras/comer_rapido.txt` (the optional cheats, copied into `cheats/` and turned off at the first start) in the program's folder.
- The NDK's Windows `make` cuts command lines at about 8 KB: `deps/build_ffmpeg.sh` installs the libraries without `make install`,
  whose header step was cut and wrote into `deps/src`.
- Test runs: `MHGU_HOME` (settings, caches, log, `shots/`), `MHGU_DATA_DIR` (the game folder), `MHGU_SAVE_DIR` (saves: otherwise
  `%APPDATA%\MHGURecomp\save`), `MHGU_CONTROL` (the control file above), `MHGU_WINDOW=1280x720`, `MHGU_FPS=1` (frame rate in the
  log), `MHGU_FPS_LIMIT=240`, `MHGU_VSYNC=0`, `MHGU_QUICKHUNT=101`. `sdlquit` in the control file closes it once `[Application]
  ConfirmExit = Off`. `MHGU_HIDDEN=1` still draws (shots, `gpuprof`) without showing a window: measured that way, nothing caps the
  frame rate but the limit, where a visible window on the desktop stopped at the monitor's refresh rate. `sdlkey F1` opens the menu
  and `mouse <x> <y> click` presses its rows and tabs.
- Measured on an RX 6800 XT (Ryzen 5 3600XT), a 1920x1080 window, no limit and no VSync: 200 to 237 fps in menus, about 205 in the
  village, 140 to 165 in a Jurassic Frontier quest (the monitor's refresh, see MHGU_HIDDEN above). Hidden, 3D at 3440x1440: the 240
  limit in that quest, the GPU's frame 1.8 to 2.3 ms, of which the window image's pass (the picture copied in, the overlay) 0.2 to
  0.6 ms. AMD's Windows driver has no mailbox present mode: VSync on is FIFO.
- Below 100 % RenderScale a desktop always uses `fsr.c`'s plain scaling (`config.c`), Low-end mode or not: the 3D picture small,
  the HUD, the text and the game's last draw at the window's size (Borderless with a smaller Resolution: the screen's, as FSR),
  without the phone's caps. It finds the start of the HUD as FSR does, so Bloom on is the usual case (checked at 50 % and 33 %,
  with Bloom on and off). The player's MSAA (the 3D's passes, before the HUD) and FXAA stay; menus' 3D models drawn after the
  start of the HUD come out at the window's size with no anti-aliasing, as with FSR (`models_aa`; the 3D resolution's help says
  so); SmoothGradients keeps H wide (`vk_fsr_input`). A desktop's Low-end mode: the menu sets RenderScale for a 3D picture about
  720 lines tall at the Resolution of the moment (`config_lowend_scale`: 66 % at 1080p, 50 % at 1440p, 33 % at 4K) and Bloom
  off, and MSAA (F7 too) and FSR are off; FXAA, shadows, texture filtering, the frame rate and reverb stay. Toaster: about 540
  lines, 30 FPS, no shadows, FXAA or anisotropic filtering (trilinear stays: NEAREST mips showed as steps on the floors).
  Leaving either, the menu also takes back an older PC build's phone values (40 %, Toaster 25 %, 30 FPS, no shadows, Toaster's
  reverb off), which its files still have. Android keeps a phone's values.
- The log is written by a thread of its own (`runtime/logwriter.c`): a line costs the thread that logs a few microseconds. Before,
  each was an `fprintf` to the unbuffered stderr, which the C runtime writes to a file a character at a time (~150 us a line), and
  with the log on a hard disk lines took up to 28 ms on the game's threads. A full 1 MB queue drops lines and the log says so; FATAL,
  a crash and exit write the queue out first (`rt_log_flush`). `MHGU_LOG_ASYNC=0`: each line written by the thread that logs.
- Thread priorities (`runtime/platform/windows.c`): guest threads above normal up to Horizon priority 43, normal from 44 on (never
  under normal, where the runtime's own normal threads kept a lock's holder off the CPU; never highest), the update and presenting
  threads, the vsync tick and the audio mixer at least above normal, background work below normal (`plat_thread_priority`). A thread
  waiting on a guest mutex lends the owner its level until the owner lets it go (`kernel.c` `prio_lend`), as Horizon does; the hitch
  log's `prio-lend` counts it. The process opts out of power throttling (EcoQoS and ignored timer resolution). `MHGU_PRIO=0`: all
  normal; `MHGU_PRIO_LEND=0`: nothing lent; `MHGU_QOS=0`: throttling left to Windows. The log's `[plat] thread priorities` line says which.
- Guest kernel waits (`runtime/kernel.c` `kwait`): each guest thread sleeps on an auto-reset event of its own, a timed wait until its
  deadline on the thread's high-resolution timer (`plat_event_wait_until`). winpthreads' timed waits end on the system's 15.6 ms tick:
  a 1 ms guest wait took 15 ms (median), a 16 ms one 31; now 1.1-1.3 and 16.3, p99 about 0.6 ms late (the timer's 0.5 ms tick here).
  A wait that times out as it is woken counts as woken, as in Horizon. `MHGU_KWAIT_PTHREAD=1`: the condition variables as before; the
  log's `[kernel] guest waits` line says which. Android and Linux: the condition variables, unchanged.

## Making the game code on the phone (S46)

The app can make `libmhgugame.so` itself, with no PC: Settings, Menu Options, **Build the game code**, or the game folder
screen's **Build on this phone** when the game code is missing or from another version (`BuilderActivity`,
`BuilderService`, `BuilderTools`, `BuilderText`). MH4U for Android has the same classes and texts (its pipeline is mh4n's
`tools/android/phone_build.py`): change both. Making the game code on the phone was Klark231's idea
(github.com/klark231/mh4u-builder); none of that project's files are used.

- **What runs.** `tools/phone_build.py` runs `tools/build_game.py <folder> --android --force --zig-cc <cc> --work <dir>
  --no-pip`: the PC tool itself, so what it checks and makes on a PC it checks and makes on the phone. It prints `@@` lines
  for the app (`@@pid`, `@@id`, `@@stage`, `@@progress`, `@@error <stage> <why>`, `@@done`); the app shows the stages and
  explains each failure in English and Spanish (`BuilderText.failure`).
- **Python and the compiler.** Both are downloaded the first time from their official sources, checked against their
  SHA-256 and kept in `files/builder` (`BuilderTools`): CPython 3.14.9 for Android from python.org (23 MB; only
  `libpython3.14.so` and the standard library, about 20 MB) and Zig 0.13.0 for aarch64 Linux from PyPI's `ziglang` wheel
  (78 MB; its 141 MB program and the 27 MB of its lib folder `zig cc -nostdlib` reads). Licences: PSF, and MIT (Zig, with
  LLVM's Apache 2.0 with LLVM exceptions and musl's MIT): `licenses/Builder-tools.txt`, THIRD_PARTY_NOTICES. Never game
  files. A download cut short goes on with an HTTP range; an unpack is done beside its place and renamed when whole.
- **Running them on Android.** An app may run a program only from its native library folder (W^X since Android 10), so
  the APK carries two small programs as `libpyrun.so` and `libzigcc.so` (`native/builder/toolrun.c`, built by
  `native/CMakeLists.txt`, copied by `build_native.sh`). `libpyrun.so` opens `$PYTHONHOME/lib/libpython3.N.so` and calls
  `Py_BytesMain`; `sys.executable` is itself, so `gen.py`'s runs work. `libzigcc.so` maps Zig's static program from the
  app's files (opening a file as code is allowed, as dlopen does) and enters it with a stack and auxv like the kernel's;
  Zig runs `/proc/self/exe` (that same launcher) again for clang and lld, and `BUILDER_ZIG_INNER` makes those runs pass
  their arguments on. Both die with their parent (`PR_SET_PDEATHSIG`).
- **Zig instead of the NDK** (`build_game.py --zig-cc`, `ZIG_CFLAGS`): the same flags, the CPU as `-mcpu=generic+v8_2a`
  (`-march=armv8.2-a`), `-nostdlibinc` with `tools/android_libc` (string.h and math.h: the game code calls only Android's
  libc.so and libm.so), linked with `-nostdlib` against stand-ins of libc.so, libm.so, libdl.so and libmain.so, and
  stripped by the link (`-Wl,-s`). Zig 0.13's clang is LLVM 18, as NDK r27's; it reserves x18 on Android as the NDK does
  (checked in a register-heavy test). GAMELIB_ID doesn't change: none of its files or its flags string changed, and the
  APK's tools zip is checked against it (`tools/pack_builder_tools.py`).
- **Resuming.** `--work` keeps the work folder: a translation of the same input by the same translator is skipped
  (`gen/<part>.done`), gen.py writes only the C that changed, and an object of the same compiler and flags newer than its C
  is kept (`obj_android/flags.txt`; one a cut left half written is made again). A run cut by Android, a reboot, Cancel or
  the heat goes on from there; `--clean` deletes the folder once the library is in place.
- **Heat and battery** (`BuilderService.watchHeat`, every 5 s): thermal status moderate, only two compilers run (the
  others SIGSTOP'd); severe or worse, the whole build waits until light; 15 % battery or less and not charging, it waits.
  Compilers at once: the free memory / 300 MB, at most the cores, half after a run that stopped while compiling.
- **Measured** on a Galaxy S25 Ultra (Snapdragon 8 Elite, 12 GB), in a shell with the same launchers, 8 compilers: the
  translation (sdk and main, 98,000 functions) about 70 s with Python at 1.8 GB at most; the 384 files in about 7 minutes
  with the tests' heat rule (paused twice at thermal status 2); the link 2 s; the work folder 668 MB. Going on after a
  failed link took 2 s (translation skipped, every object kept). The library (109.7 MB) has the NDK one's needed
  libraries, soname, BIND_NOW and exported symbol; it lacks only crtbegin_so's three imports (`__cxa_atexit`,
  `__cxa_finalize`, `__register_atfork`), and its read-only data has a segment of its own. Not yet run in the game.
- **The PC route** is unchanged: without `--zig-cc` and `--work`, the NDK build is made as before (a temporary work
  folder, the same objects and link), and the library and its id are now put in the folder whole (written beside, then
  renamed).

## Testing on a device

A test build (`MHGU_DEV=1`) reads two files in `Android/data/com.mhgu.recomp/files`; a public build reads neither:

- `env.txt`: `NAME=value` lines. `MHGU_QUICKHUNT=101` goes straight into a quest (`MHGU_QUICKHUNT_AREA=5` with it: the quest
  starts in zone 5 of its map, not the camp; quest 203, Village 2 stars, is Verdant Hills), `MHGU_VK_STATS=1` logs statistics,
  `MHGU_TOUCH_LOG=1` logs fingers. `MHGU_HOME=<folder>` moves the settings and the log, but not the saves: those are
  `Android/data/com.mhgu.recomp/files/MHGURecomp/save` (or the Save folder chosen: `save_folder.txt`), so copy them first. `MHGU_CAMLOG=1` logs the hunt camera and the monsters,
  `MHGU_LOG_MOVES=1` the hunter's actions, `MHGU_LOG_NET=1` the network, `MHGU_PAD_DEBUG=1` what SDL reads from a controller,
  `MHGU_FORCE_NOLSE=1` the adapted game code on any CPU, `MHGU_BCN_LOG=1` the time big BC textures take.
- `control.txt`: appended commands, read while the game runs. `A 8`, `DOWN 8`, `type <text>`, `textkey enter`,
  `memsave <hex address> <hex size> <file>`, `peek`, `poke`, `gpuprof` for one frame's time per render pass, `hudtrace` for one
  frame's HUD layers (and each pass's targets and area, each draw's program, viewport, scissor and textures, copies and dispatches),
  `plpos` for the hunter's place, `STICK_UP 25` and the like to walk, and `crash` for the error screen. Create `control.txt` from
  the shell with `chmod 666`, or the app can't read it.
- Another GPU's paths, for bugs only some phones show: `MHGU_VK11=1` (Vulkan 1.1), `MHGU_VK_NO_INT64=1`, `MHGU_VK_NO_BC=1` (BC
  decoded on the CPU), `MHGU_VK_NO_DIVISOR=1`, `MHGU_VK_NO_BORDER=1` (no custom border colours), `MHGU_VK_D24=0` (D32S8 depth) and
  `MHGU_VK_ADRENO=1` (the Adreno-only workarounds on any GPU; `0` leaves them out on an Adreno). `MHGU_ASPECT=1612:720` gives a
  4:3 screen a wide picture (letterboxed) and `MHGU_CONFIG=<file>` another ini, so a phone's own settings stay untouched.
  A GPU without custom border colours logs each border colour the game asks for and the standard one it gets instead.
- `MHGU_FSR_LOG=1` (FSR, and Low-end mode's plain scaling too) names the draw where the HUD starts and the 3D effects kept with
  the scene before it (vertices, depth test, the textures' sizes, `r` for a render target); `MHGU_FSR_SCENE_FX=0` lets those
  effects start the HUD again, as before 0.1.9.
- Performance switches (`MHGU_PERF_<NAME>=0|1`, read once at the start). Unset, each has its default: on where 0.1.9's
  performance pass measured a win (the list below), off (the old way) for the others; `=0` or `=1` in `env.txt` or the
  environment overrides it for a test, and the log's `[perf]` lines say which are set. On a Vulkan 1.1 driver (`g_vk.api12`
  false, `MHGU_VK11=1` too) the ones that change what the GPU is given (`IPA`, `VSTRIM`, `DYNUBO`, `RODEPTH`, `SHADOW_CLEAR`,
  `FXAA_COPYDRAW`, `HUDSTART_FUSE`) go by the GPU's family (`device.c vk_v11_switches`, the log's `[perf] a Vulkan 1.1 driver`
  line): an Adreno's all on, as on a newer driver and as in 0.1.9; a Mali's all off, `IPA` too, as in 0.1.9.1; any other's
  (a PowerVR's) `IPA` alone, as in 0.1.9.2. The players' logs behind it: 0.1.9.1 turned them all off on every 1.1 driver
  (a Mali-G57 MC2 on 1.1.177 drew the 3D picture almost black with 0.1.9's defaults), and then Adreno 610 and 618 phones on
  1.1.128 lost the device every few minutes (KGSL errno 71, at a town's load, an area change, in hunts; a Galaxy A52 on that
  driver had played 0.1.9 without it) and a Redmi Note 8 went from 35 to 17 FPS. 0.1.9.2 turned `IPA` on again for every 1.1
  driver: an Adreno 618 on 1.1.128 (Toaster, 33 %) drew the Volcano's distant plants and dead trees as black bars and white
  noise without it, and right with it in 0.1.9 (the player's two videos); and Mali-G57 MC2 phones (Helio G99, Dimensity 6080)
  drew the stage black again with it (ground, buildings, sky, Shagaru Magala; the hunters and the effects right: one player
  saw it go with 0.1.9.1 and come back with 0.1.9.2). An Infinix with that Mali and driver showed 0.1.9's whole set right at
  Toaster 66 %, so the Mali's culprit isn't proven to be `IPA` alone: they stay off together there. `MHGU_VK_ADRENO=1` or
  `MHGU_VK_MALI=1` (or `=0`) with `MHGU_VK11=1` takes a family's choice on another GPU. On the Moto g06 (Mali-G52, a 1.3
  driver) `MHGU_VK11=1` alone names the Mali and draws quest 101's camp right with them all off, and `MHGU_VK_ADRENO=1` with
  them all on: neither shows the 1.1.177 driver's black. Without `IPA` a varying the shader
  multiplies by 1 / w is read as v * gl_FragCoord.w * (1 / w), and far away that round trip through position w is all a
  driver's precision has to spoil: a test build reading gl_FragCoord.w at half precision there turned the Galaxy S25's distant
  cliffs flat and its sky black; the varying read as it is doesn't use w at all:
  - `MHGU_PERF_INPUT_WAKE=1` (Android): the window loop (`ui.c ui_run`) sleeps its 3 ms between polls only while something
    runs by the clock (`host_input_busy`: a button or a stick given to the game, a finger down or a touch timer, `touch_busy`,
    the lock-on, a controller SDL reads by polling (HIDAPI's: Android's own come as events), the game's rumble, the settings
    menu, a scripted press, an event in the last 500 ms);
    otherwise it waits up to 16 ms on a semaphore that SDL's event watch, each frame presented, a rumble changing and the
    menu's requests post. Woken by an event it brings the controls' state up to date first, as the pass 3 ms before did. The
    touch controls' timings are those of the 3 ms loop. Once a minute the log counts the passes of each kind.
  - `MHGU_PERF_OVERLAY_REUSE=1`: a frame with the touch controls alone on the overlay (no menu, notice, chat, stats, credits or
    plugin's drawing) whose key (`touch.cpp DrawKey`: their state, what shows and where, the HUD parts' places, the settings'
    generation, the game's buttons on the badges, the screen) is the one of the frame built last is not built again
    (`overlay.cpp frame_once`): the render thread draws the copy it has. Nothing moving (a finger, the run, the lock-on's
    pulse, the menu pad's fade) gives no key; once a second a frame is built anyway. `=2` is a check: those frames are built
    and compared with the copy, and a difference is logged. The log counts built and reused frames each minute.
  - `MHGU_PERF_MENU_PRESENT=1` (Android): while the settings menu holds the game (`overlay_frame`), the held picture is
    presented again only when the menu's draw lists changed byte for byte (`snap_same`), when the window needs it
    (`vk_present_needed`: back from the background, a new size, VSync) and once a second; the menu is still built 60 times
    a second. Each hold logs its presents and the frames it didn't present again.
  - `MHGU_PERF_NO_ACCEL=1` (Android): SDL's accelerometer listener (50 events a second on the app's main thread, which nothing
    reads: the game's motion sensors are always at rest) is not registered; the display's rotation reaches SDL from a
    `DisplayManager` listener instead (`GameActivity.GameSurface`), as `onNativeRotationChanged`. With the variable set,
    0 or 1, the log names each display orientation SDL reports.
  - The others, each described with its part of the runtime below: the translator's `NMINMAX`, `IPA` and `VSTRIM`; its per-draw
    work's `DRAWS` (`GETENV`, `DRVTAB`, `TEXSCAN`, `TEXTIME`, `PIPEMEMO`, `LAZYIDX`, `IDXCOPY`), `DYNUBO` and the check `CHECK`; the
    safety nets' `VCNET`, `TEXNET`, `TEXSAMPLE` and `TEXSTREAM`; the render passes' `RODEPTH`, `SHADOW_CLEAR`, `HUDSTART_FUSE`,
    `FXAA_COPYDRAW` and `FMTLIST`; pacing and threads' `ADPF70`, `EFFICIENT`, `LIMITER`, `MEMTHREAD`, `THREADNAMES` and `NODISCORD`;
    the window image's `PRESENT_TIME` and `PREROTATE`; the audio's `AUDIO_SDEST` and `AUDIO_ADPCM` (the check `AUDIO_CHECK`), the
    guest's `SLEEP_COARSE` and the dispatch's `DISPATCH`. Each works on its own and with all the others: the RG406V (Mali-G57, its
    own settings: 75 %, 60 FPS, Shadows On, FXAA Off), quest 101's camp, standing, all of them on at once (`SHADOW_CLEAR=2`,
    `HUDSTART_FUSE=3`): the game's and the settings menus as before, and of five shots of each run, the 143,715 pixels that stayed
    the same within both runs (the near ground, the HUD) the same in both, 0 differing; the rest moves as much within a run as
    between the two. With the checks on as well (`CHECK=1`, `OVERLAY_REUSE=2`, `AUDIO_CHECK=1`, `MHGU_TEX_NET_CHECK=1`,
    `MHGU_VK_VC_SAFETY=2`) 8.5 million per-draw comparisons, the mixer's splitter volumes and ADPCM blocks, the vertex cache's
    slices, every texture's whole hash and the reused overlay frames found no difference. That phone has no ADPF session (API 33):
    `ADPF70` and `EFFICIENT` do nothing there.
  - Measured (0.1.9's performance pass, the quest-101 camp standing, the Anbernic RG406V (Mali-G57) and the Moto g06 (Mali-G52);
    the picture 0 pixels different where the scene stood still with each one, no errors, no device lost) and so ON by default:
    the render passes and translator together (`SHADOW_CLEAR=2`, `RODEPTH`, `FXAA_COPYDRAW`, `NMINMAX`, `IPA`, `VSTRIM`): the
    RG406V's GPU frame -16 % at 75 % (p99 33.4 -> 17.0 ms) and -20 % at 100 % (45.9 -> 53.3 FPS), not measured on the Moto;
    `HUDSTART_FUSE=3` (the default is 3): the Moto at Low-end 60 GPU frame -10 %, the draw giving the blit's bytes; `DRAWS` (all
    seven) and `DYNUBO`: the Moto's translating thread -7.5 %, descriptor sets reused 25 % -> 67 % (the RG406V: `DRAWS` a small
    win, `DYNUBO` neutral); the safety nets (`VCNET`, `TEXNET`, `TEXSAMPLE`, `TEXSTREAM`): the Moto's uploads -66 %, bytes copied
    -33 % (the RG406V: the same CPU, the work moved to the workers); `PRESENT_TIME`: the Moto at Toaster 30, 98-99 % of the
    intervals exactly two vsyncs (76-79 % before), p90 49.9 -> 33.3 ms; `LIMITER`, `MEMTHREAD`, `SLEEP_COARSE`, `AUDIO_SDEST`,
    `AUDIO_ADPCM`, `INPUT_WAKE`, `OVERLAY_REUSE`, `NO_ACCEL` and `NODISCORD` together: the RG406V's app CPU -3.9 % (the old
    `[mem]` line took 8-15 ms of the presenting thread once a minute); `PREROTATE`: exact, the Moto's layer from the MediaTek
    MDP rotator straight to the panel (`UI`, transform 0), nothing on the RG406V (its DPU turns it); `THREADNAMES` (names only).
    The PC build shares the runtime and has the same defaults (measured there: the safety nets only); `PREROTATE`,
    `PRESENT_TIME`, `INPUT_WAKE`, `NO_ACCEL`, `NODISCORD`, `MEMTHREAD` and `SLEEP_COARSE` are Android's alone.
  - `ADPF70` is ON since 2026-11-10: unplugged Moto g06, Toaster 30 at the quest 101 camp, one ABBA pair of 9-minute windows: off 2.832 W,
    on 2.789 W (-1.5 %), app CPU -4.8 %, the same 30 FPS and pacing (10-second samples agree within about 0.03 W). Before that it was:
  - Measured and left OFF: `ADPF70` (the Moto's CPU -3.8 %, but its watts unknown: the phones were charging; measure them
    unplugged before turning it on), `EFFICIENT` (nothing at the camp), `DISPATCH` (no change). Not measured, OFF: `FMTLIST`,
    `MENU_PRESENT`, and the checks `CHECK`, `AUDIO_CHECK` (and `OVERLAY_REUSE=2`).
  - Not yet checked on an Adreno (Galaxy S25) or a PowerVR: `RODEPTH`, `SHADOW_CLEAR`, `HUDSTART_FUSE`, `FXAA_COPYDRAW` (each
    checks itself on the GPU before it acts, and logs it), `NMINMAX` (Vulkan 1.2 drivers but a PowerVR's), `PREROTATE` (the
    Galaxy held at 90 degrees: `dumpsys SurfaceFlinger`'s layer, the menu's text), `PRESENT_TIME` at 120 Hz (the swapchain line
    once, not again and again).

What the app writes to `cache/` under `Android/data` can't be deleted over adb, so a test folder under `MHGU_HOME` stays until the app removes it.

The app's own files there (`mhgurecomp.ini`, `touch_layout.txt`, the saves) belong to its user: the shell can't read the saves, so
they can't be copied over adb (a test that may write them needs another way back). A file the shell makes anew (`sed -i`, `adb push`, `rm`
and `cp`) belongs to the shell, and the app can't read it: an `mhgurecomp.ini` made so runs the game on the defaults and saves nothing.
Change one with `cp` onto the existing file (it keeps its owner); if one was made anew, delete it, start the app once (it writes the
defaults) and `cp` the copy over that.

Empty `control.txt` before starting: old commands replay. `3D resolution` needs a restart: until then the menu says so and the old
one is still running.

**Read frame rates from the on-screen counter.** `[vk] 2s waits` counts submits, and a frame takes two of them.

Android killing the game shows in `adb logcat` as `lowmemorykiller: Kill 'com.mhgu.recomp'`.

## What real devices taught us

- Mali has no BC textures, so they are decoded on the CPU and take 4 to 8 times the memory. BC4 and BC5 go to R8 and RG8, and under
  5 GB of RAM textures with mipmaps lose their largest level (`mip_skip`, `nvn/vulkan/textures.c`). Without this a 4 GB phone was killed.
- On a Mali, more than half of a hunt's frame is some 30 full-screen passes, about 0.15 ms each whatever their size. Low-end mode skips
  the bloom and depth of field chain (`lowend_skipped`, `nvn/vulkan/exec.c`), and the two draws that add it to the picture must go too.
  A render target that is never drawn gets uploaded from guest memory when sampled: that garbage, added over the picture, is coloured noise.
  So skipped targets are marked with `renderer_mark_gpu_owned`. Below 100 % it also keeps the HUD at the screen's size through
  `fsr.c`'s plain scaling path, without FSR. `MHGU_VK11=1` was tested with it, only by forcing that path on a newer driver.
- With FSR or Low-end mode's plain scaling, everything from the start of the HUD on is drawn on output-size stand-ins over a
  depth target cleared to far (`fsr.c`). A 3D effect the game draws into the full-screen image after its post passes (Astalos'
  charged wings) taken for that start was drawn over the hunter and the monster, and sampled render-size images at output-size
  places: streaks where a sampler clamps, the picture repeated where it wraps. A draw there tested against the scene's depth, or
  sampling the picture (that image itself, or a render target of the render size or half of it), stays with the scene (#60).
- FXAA leaves a 2 px green line at the edges on Mali and costs about a fifth of the frame rate, so it is off by default on Android.
  The cause is unknown.
- A Vulkan 1.1 driver (seen: Mali-G57 MC2, 1.1.177) has no timeline semaphores: each submission gets a fence and one thread waits for them
  (`nvn/vulkan/exec.c`). It also gets SPIR-V 1.3 and no FSR. `MHGU_VK11=1` in `env.txt` forces that path on a newer driver.
- That same Mali has no `shaderInt64`: with `maxwell_no_int64` the shaders do their 64-bit math in 32 bits (`nvn/maxwell/translate.c`).
  `MHGU_VK_NO_INT64=1` forces it. A forced path only shows that the path works: a real old driver can still differ.
- A driver can make more window images than it was asked for. Never assume the count: `VK_SC_MAX`, `nvn/vulkan/device.c`.
  `MHGU_VK_IMAGES=<n>` asks for that many.
- Samsung ignores the manifest's game category. Only the one set by the installer counts: see `GameFolderActivity.declareGame`.
- On Android, a `SUBOPTIMAL` result from present must not recreate the swapchain: doing so cost two thirds of the frame rate.
- Frame pacing at an FPS limit that is a whole fraction of the refresh rate (30 on 60 Hz): presented as soon as drawn (MAILBOX),
  the frames land on either side of a vsync and show for 16 and 50 ms in turn. Anbernic RG406V, Low-end 50 %, 30 FPS, standing
  at the quest-101 camp, display side (MH4U's `scripts/pacing/probe.sh` and `report.py`): 7.7 % of the intervals a vsync short or
  long, 1% low 20 fps, p99 50 ms, 62 intervals of 50 ms a minute. `MHGU_PERF_PRESENT_TIME=1` (`nvn/vulkan/present_pacing.c`, read
  once; on by default since the measurements above) presents FIFO at those limits, each frame timed on the display's vsync grid
  (`VK_GOOGLE_display_timing`, MH4U for Android's rule; the margin is `GameActivity.presentationDeadlineNs` and 4 ms, raised 2 ms
  for good at a second late frame within 30 s): 0.04 % of the intervals off, 1% low 27.9 to 29.3, p99 33.5 ms, game time 1.00x.
  Android 10's libvulkan (11's too, as far as is known) crashes destroying a timed swapchain that was replaced (its frame
  timestamps turned off on a null window: SIGSEGV at 0x98, a POCO X3 NFC at its first start at 60 FPS, #203): there the old one
  is destroyed first and the new one made without it (`create_swapchain`; `MHGU_VK_SC_FIRST=1` does it on a newer phone). A
  driver whose reported rate keeps changing (that phone: 120 Hz for one swapchain, 50 for the next) made the swapchain again every
  2 s: the fourth change within 30 s keeps presents untimed for the session (`paced_change`). On the Moto g06 (Android 14,
  `MHGU_VK_SC_FIRST=1`, 30 FPS, `prerotate 0` and `1` in the control file) the timed swapchain was made again four times so,
  the picture right both ways round.
  `vkGetRefreshCycleDurationGOOGLE` said 16.65 to 16.68 ms where the vsyncs come every 16.667: the period is measured from the
  presents' own times, and every slot put on the newest vsync's grid (chained on the period given, the slots drifted half a
  period in 15 s and a frame showed a vsync late each time). `MHGU_PRESENT_MARGIN=<us>` fixes the margin (tests). At the refresh
  rate itself (60 on 60 Hz) nothing changes: MAILBOX drops 0.6-0.8 % of the frames drawn there (shown 59.5-59.6 of 59.96 a
  second, 1% low 33-36), under the 1-2 % that would pay for FIFO's latency. The game never asks `nn::vi` for its vsync event
  (`vi.c`'s thread doesn't start): its pace is the engine's limiter alone, on the host's clock.
- `MHGU_PERF_PREROTATE=1` (`device.c` `sc_turn`, read once; on by default, `=0` the old way): the window image drawn turned for the
  panel. Presented upright in landscape (IDENTITY), the picture is turned by the system every frame: an Anbernic RG406V's DPU does
  it as it scans out (`dumpsys SurfaceFlinger`'s composer table: DPU, transform 90), but a Moto g06's MediaTek composer sends the
  layer through its MDP block (`MM` in that table), which reads the whole picture and writes a turned copy. Turned by the app the
  layer goes straight to the panel (`UI`, transform 0). The swapchain takes the surface's transform and portrait size;
  `vk_present_draw` (`exec.c`) turns its rectangle and samples the picture turned back, the overlay draws a turned copy of its
  snapshot (`overlay.cpp`: vertices, and its scissors as whole pixels turned, as the backend cuts them upright) and a screenshot
  is turned back upright. Only at 90 degrees (transform 0x2, as the three phones are usually held): the settings menu held over
  a frame, screen captures with and without it are identical (0 of 691200 pixels). At 270 (0x8) Vulkan's top-left rule puts
  pixel centres on ImGui's glyph edges (on half pixels across) on the other side: 266 pixels of the menu's text differed, up to
  48 levels, so the system keeps turning there. A 180-degree turn of the phone makes the swapchain again (a `SUBOPTIMAL` checks the
  transform, 4 a second at most). `prerotate 0|1|2` in the control file switches it in a running game (2: at 270 too), for
  comparing the same held frame both ways.
- The hunter has one class per weapon: never compare against one vtable. Virtual 94 tells a hunter of any weapon and whether the
  weapon is out (`runtime/hle/weapon.c`). The `sPlayer` slot can hold another object that points to the hunter.
- Don't trust an offset measured in one session: check it in another one.
- The game code is built for ARMv8.2, and its 2564 `casal` / `swpal` are illegal on an ARMv8.0 CPU (Cortex-A53, A73 alone): SIGILL at the
  first critical section. `runtime/gamelib_lse.c` writes a private copy of the library where each jumps to an exclusive load / store
  sequence in an added segment. `MHGU_FORCE_NOLSE=1` tests it; no device here lacks LSE, so only logs of those phones confirm it.
- A LAN search has to wait: on Wi-Fi a broadcast waits at the router for a sleeping device's next beacon, and 400 ms found nothing. It
  now listens 1.5 s and asks every 0.5 s, on each interface's own broadcast address too, with a Wi-Fi multicast lock held.
- `touch_hud_menu` decides the touch layout from the HUD's layers. Layer 97 is the game's menus, but aiming a heavy bowgun draws there too
  (`weapon_aiming`), and the multiplayer menu and the hub form are on layer 100, as stretched panels, which the HUD's parts there are not.
- A menu's left windows on layer 100 keep the middle 16:9 with its windows at 21:9 and Edges. Some are drawn before the menu's windows
  in the frame (the smithy's parchment and its model, the quest counter's quest card: under the window beside them), so a window of
  this frame or the two before tells (`ui_window_frame`); by the pass alone they went Right, over that window. hudtrace marks them.
  A town's world map (X) and its "Choose a destination" list are on layer 100 too (x 291..985, y 192..522), with no window of
  layer 97 beside them: in a town (`g_town_ns`, its update within 500 ms) layer 100 above y 600 and left of x 1000 keeps the
  middle as well, its prompts at the bottom right the edge. Up to 0.1.9.3 the map and the list sat right of the middle with
  Edges / 21:9 (128 of the canvas's units at 1600:720, hudtrace on the RG406V: GU#219).
- Layer 110 holds the loading screen's strips (y 168, 433, 570, the 168 ones first in the pass) and, in the field, a gun's
  quick-aim reticle (one piece at about 650, 433), the lock-on markers and effects: those keep the middle at any HUD anchor, as
  the 3D picture they are on does. Up to 0.1.9.3 the reticle went right by place with Edges / 21:9, and in frames where its y
  fell within 0.3 of 433 it was taken for a strip, with the status after it at the loading screen's places (RG406V,
  `MHGU_UI_ANCHOR=Edges`, `MHGU_ASPECT=2340:1080`, Valor heavy bowgun training: GU#219).
- The hunter: heading at +0x14a0, in the camera's units, (x, z) = (sin, cos); written, the hunter turns at once. Place at +0xa0, zone at
  +0x1054, which monsters have too. The classes are `uPlayerQuest00..15` in weapon order, no 05: 04 and 06 the bowguns, 10 the bow.
  Writing the place does nothing: the game puts it back. Each zone of a map has its own coordinates.
- The lock-on follows the Target Cam's monster, as the MH4U port's (`hle/mousecam.c lock_target`): the hunt camera's +0xa6c, 0 none, n the
  n-th large monster (+0xb5f7 0) of sEnemy's slots. Measured in two sessions in the Arena (quest 630, two large monsters: its camp is zone 0,
  the floor zone 1): ZL took it 0, 1, 2, 0, and the game's L turned the camera to the first, then the second. Until the hunter has seen the
  monsters their icons read Hidden and ZL does nothing. The Control Type, and so the change-target button, is `sUserInfo` +0x446c (0 Type 1
  ZL, 1 Type 2 the d-pad, 2 Type 3 R3: `mousecam_target_buttons`); with L/R Button Controls swapped (+0x446e) Type 1's is L, and the
  camera button then presses ZL in a hunt, the camera centring (`camera_buttons`, touch.cpp). The target camera's icons: one monster at x 1200 (y 42 and 57, the
  button's hint at 82), two at x 1170 and 1230 (y 42) with the hint still at 1200 (`target_piece`, nvn/vulkan/exec.c).
- The lock-on is turned on and off by the touch controls' Lock-on button (`C_LOCKON`, `[Touch] ShowLockOn`, on by default; `lock_on`
  in touch_layout.txt), the port's own: it presses none of the game's buttons (the owner's call, 2026-11-12: the game's L caused
  problems). Up to 0.1.9.2 it was the padlock by L and a long press on L, which let L go after 600 ms: L held lost its own uses (the
  item bar's L + Y / A; MH4U's bowgun ammo with L + X / B, on Discord). It sits on the right thumb's arc left of L, shows in a
  hunt only (in a town in the editor alone) and while `[Controls] LockOn` is on, lit with a ring while the lock-on is on. A
  controller keeps the right stick's click. The MH4U port has the same button (name, texts, icon, place). In a quest the lock-on no
  longer goes off by itself after 10 s without the hunt camera while a quest's hunter is there (a mount turned it off: #220).
  With it on, a press of A standing still is a locked shot only while the hunt camera runs or the hunter is in an aim's actions:
  crouching fire of a style whose actions aren't `crouch_fire`'s (Valor's) got the help's R and its A held back (#209).
- In towns the game's camera does not turn, with any stick: it isn't a controller problem.
- SDL makes an Android controller a gamepad mapping only if its button mask has a face button, and the mask comes from
  `InputDevice.hasKeys`. A pad whose A, B, X, Y Android doesn't list (though it sends them) was a joystick never opened: SDL
  sent its A and B as Enter and Escape, so the port's menu answered them by their labels and the game read nothing (#87).
  `SDLControllerManager.faceButtonPressed` adds it again with the four at its first press of one. A controller can be faked
  without one: the shell is in the `uhid` group, and `hid <file>` (JSON lines: `register` with a HID descriptor, `report`,
  `delay`) makes a Bluetooth HID device the app sees as real. Tested so on a Motorola g06, its face buttons hidden by a
  test-only line in `getButtonMask`.
- Heat: `GameActivity.pickRefreshRate` takes the lowest screen mode that fits the FPS limit, never under 60, because forcing the fastest one
  shows nothing more. `efficiency_follow` (`android_perf.c`, API 35) lets the system use efficient cores while frames use under
  half the target, and stops at once when they near it. `MHGU_ADPF_EFFICIENT=0` turns it off. A phone that isn't charging gives real watts:
  `/sys/class/power_supply/battery/current_now` and `voltage_now`. Measured in a hunt, standing: the FPS limit is what counts. Moto g06 at 60 FPS
  3.37 W, at 30 FPS 2.70 W; S25 Ultra at 120 (106 real) 3.48 W, at 60 2.33 to 2.73 W. 60 Hz against a faster screen showed no gain beyond the noise on either.
  Toaster at 30 FPS on the Moto, at the quest-101 camp (#178): Shadows On 2.75 W, Off 2.68 W (ABBA, 4-9 min, two runs each), +2.5 %,
  no more than two runs of one setting differ from each other. The shadow pass is 6.2 ms of a 29 ms GPU frame there, Off 25 ms: at 30 FPS the
  GPU drops a clock step instead of idling (Off, its other passes took about 10 % longer), so the 6 ms saved little power.
  A Galaxy denies those files: use `dumpsys battery` (Charge counter, in steps of 4.9 mAh); its `dumpsys thermalservice` repeats a stale value.
- Pacing and threads, 0.1.9's performance pass (each read once at the start; `LIMITER`, `MEMTHREAD`, `THREADNAMES` and `NODISCORD` on
  by default, `EFFICIENT` off; `=0` / `=1` override): `MHGU_PERF_ADPF70` the ADPF target 70 % of the frame period at FPS limits of 60 and below, as in MH4U
  (at all of it the system lowers the clocks until the busiest thread fills the period: the Moto at Toaster 30 worked 25.5 ms a frame on
  average, 33 at most, of 33.3), the efficient cores then judged against the period. `MHGU_PERF_EFFICIENT` a frame over the target on
  its own neither turns the efficient cores off nor keeps them away (`efficiency_follow`; two in a window, or another within 30
  windows, still do); `MHGU_ADPF_EFFICIENT=force` holds them on, to measure what they can save. `MHGU_PERF_LIMITER` the engine's frame limiter
  (main 0xda5824) sleeps until its frame's time less a margin instead of turning 1-2 ms a frame on the clock (`runtime/hle/limiter.c`;
  `MHGU_LIMITER_LOG=1` its sleeps and turns, on or off). `MHGU_PERF_MEMTHREAD` the minute's `[mem]` line on a thread of its own, off the
  update thread (with `MHGU_ADPF_LOG=1` each line says what it cost). `MHGU_PERF_THREADNAMES` names the runtime's threads (`g44-10a` the
  game's by priority and handle, `game+render`, `submit`, `vsync`, `audren`, `upload-N`, `shader-N`, `fence`, `ui`...): otherwise all are
  `SDLThread`, and the MH4U port's pacing probe (`threads.py`) can't tell them apart. `MHGU_PERF_NODISCORD` no Discord thread on Android.
- `MHGU_FS_NEW=1` logs each fragment program the first time it draws; `MHGU_FS_WATCH=<hash>` saves the frames where one draws. A hit draws
  `cc207b43683737c8`, a 17-sample radial blur. The device's `shots` folder keeps old files: go by the names in the log, not by what's there.
- Started with the phone locked or upright, the display says portrait: the 3D picture came out 720x576 with black at the sides. `config.c` now
  swaps the two. Start test runs with the screen on and unlocked anyway: `am start` on a locked phone doesn't show the game.
- The game's Bow Controls (`sGameControl` +0x6c): Type 1, X draws and R enters the aiming mode; Type 2 the other way round. Read `weapon_bow_type`, or a
  bow's layout presses the wrong button. Letting go of a button that doesn't draw does nothing, which looks like a broken bow.
- `HN_WeaponControlsMsg_eng.gmd`, `HN_HunterTipsMsg_eng.gmd` and `StartMenuMsg_eng.gmd` in `romfs/nativeNX/eng/table` are the game's own manual: read them
  before guessing what a button does. L + X and L + B change the ammo, and the game takes L only after about 300 ms.
- Running with the stick (`[Touch] StickRun`, on by default; `touch.cpp` `run_*`, `lock_*`, the same constants, drawing and
  texts in the MH4U port's touch.cpp). A zone farther out that locked by itself was tried and dropped: the old feel stays, the
  thumb past 1.6 stick radii runs (R) and lets go under 1.45, sheathed only, as in 0.1.8, plus Call of Duty Mobile's sprint
  lock: a small circle with a padlock 2.4 radii above the stick (0.02 screen heights below the top at least) fades in as the
  thumb pushes up past the rim; the thumb on it (within 0.55 radii of its middle) lights it (armed, `run_over`), and only
  LIFTED there does the run lock, with a click (the owner, 2026-11-10: it locked as soon as the thumb passed over it, and
  with the thumb still down the lock held the stick full forward wherever it went, so it seemed never to let go). It goes
  on forward (the stick held full up: the camera steers) until the stick or the lit circle is touched again, the weapon
  comes out, a menu opens or the hunter isn't played (an area change too: `run_end` logs why under MHGU_TOUCH_LOG). It locks
  for a thumb that landed anew (not in the circle) or came back inside the rim since a lock ended (`run_armed`). X holds the run's R back for 400 ms: a thumb at the rim through a sheathe started R in the same update
  as an X + A, and the game read R + X + A (#151). Off: a Run button (`run_button` in touch_layout.txt: the old left-side
  `run` line every saved layout has is dropped), held only, dimmed with the weapon out, hidden in menus; slid onto an attack
  it is that attack; in a corner layout saved before it, it starts in the band's hollow.
  `[Touch] GuardSlide` (off by default): a thumb from Guard keeps R as it slides onto the attacks (`Finger.keep`), and is
  R + Y on the item bar.
- The item bar's long press (#109): bit 0 of the hunter's word +0x2770 is the bar open (its neighbouring items shown); the
  item bar's handler clears it and sets it while L is held (vfuncs +0x25c / +0x260 / +0x264 test, set and clear its bits).
  `itembar_hold` sets it after the handler, past the same checks, with no L pressed (L would turn the camera).
- The aim's pitch: hunter +0x14b8 (a float, -100 down to 100 up) and +0x2714 (16-bit units, 45 degrees up, 30 down). A shot that isn't aimed ignores them, so a
  locked shot from a bowgun goes through quick aim (`touch.cpp`, `assist_*`). With a gun aimed or a bow drawn the hunt camera's handler does not run: per-update
  work goes in the item bar's hook (`hle/itembar.c`).
- Locked shots come only from a normal stance (#177, #132): `weapon_stance`, the weapon out, no weapon move, no aim, and `mousecam_hunt_camera` (the hunt
  camera's handler ran in one of the hunter's last two updates, counted: `mousecam_active` stays true 200 ms after it stops), and a monster to aim at
  (`mousecam_lock_aimable`). A press of A there, with no L, ZL or ZR held (the item bar, Control Type 3's Hunter Arts), waits the game's own
  two-button window (`combowin_ns`), so X, B, Y or a shoulder joining it pass with it as pressed, and only then holds R; the aim counts as up by
  `weapon_aiming` or by the hunt camera stopping (any style's aim), never while still going in (0001:045). The player's own R, crouching fire, reloads,
  dodges and the styles' moves pass untouched, and `mousecam_lock_shot` writes a bowgun's heading and pitch only during the help's own aim; a bow's only
  for a draw from the stance (or one chained in its shot), never in its aiming mode. `ranged_aim` keeps an aim weapon_aiming doesn't know (the camera
  stopped, A, R or X down just now) from being a menu. Measured on the RG406V with a heavy bowgun (2026-11-10, `MHGU_LOG_MOVES`, `MHGU_CAMLOG`):
  - R is a tap (the scope: 0001:046 with the weapon's command table 43) when let go within about 250 ms of the game's time: 14 frames at 60 FPS, 8 at
    30; 15 and 9 are the quick aim. While it goes in (0001:045, about 22 updates at 60 FPS) the game drops A, B, X and Y early on, and its camera stops
    some updates before 0001:046. So the help's R, ended by B, X or Y, stays down for `aim_hold_ns` and those buttons reach the game after it.
  - 0001:046 stays the action after the aim or the scope is over, standing, with the hunt camera running again: `weapon_aiming` counts it only while
    the hunt camera is stopped. After a shot the game leaves the aim (0001:000) with R still held, so each press from the stance aims anew (~440 ms).
  - The hunt camera stops in the quick aim and the scope and runs in crouching fire (0010:026, 028, 029, 033; 053 its dodge). A shot is 0010:005 then
    124, an empty magazine's press 0010:004, a reload 0010:001. No action had bit 15 set.
- Standing still (no stick from any input) a press of A alone is 0.1.8's locked shot again (`still_wait`, `touch_lock_still`; the
  owner's call, 2026-11-10): R at once, A once the aim is past going in (0.1.8 fired in 0001:045, where the game drops A: its first shot
  from the stance never left), the hunter turned to the monster from the press to the shot; in an aim's actions (0001:045-048 with any
  camera, `weapon_aim_action`) A passes at once. Walking, the stance's shot above. The separate attack buttons' seam (X + A) is hit
  only where those buttons are: in the ranged layout it could sit under the fire button (the owner's layout: every tap was crouching fire).
- The aim button: a tap is the game's tap of R (the scope; a bow's aiming mode, 0010:012, which its button toggles); held or slid (the
  slide is a drag of the view) R is kept down past `aim_hold_ns`, and a bow's button is pressed again once the hunter is back in 012.
  Measured on a Moto g06 with the hbg and bow test saves: the scope is command table 43, the quick aim 0001:046 with table 0, a bow's
  shot from its aiming mode 0010:002, 001 and back to 012.
- The item bar's arrows are controls (`C_ITEML`, `C_ITEMR`, saved as `item_left` / `item_right`): at their default places they follow
  the bar (`arrow_follow`; in the editor the bar's frame, `arrow_bar`), a drag gives them a place of their own. The layout editor holds
  the game as the settings menu does (`menu_holds_game`), and the HUD's last picture counts as just drawn meanwhile (`hud_now`).
- `[Controls] GyroAim` / `GyroSensitivity` (the last fields of `Config`): `host_input.c gyro_update` opens SDL's gyroscope (a
  controller's if the one read has one) only while `weapon_gyro_aiming`, and adds its turn to the mouse counts. The axes go by
  `SDL_GetNaturalDisplayOrientation` (an RG406V's is landscape). `gyro <right> <up> <ms>` in control.txt fakes a turn (degrees a second).
- A menu can't be told from a window over the hunt: no flag in the hunter or the game's globals changes, and the hunt camera runs under menus too. `game_menu`
  treats a window as a menu when the hunter isn't being played or a button that opens one was pressed just before. With a bow or
  a gun out in a quest only Plus or Minus count (and a cursor running makes a window a menu only after one of them): the aims and
  the scope draw on layer 97 and outlast the aim (0001:046 with the hunt camera back, not `weapon_aiming` since #177), and the
  lock-on's target button presses ZL all the time, so a heavy bowgun's hunt on the Galaxy went to the menu layout. Without a window,
  four seconds without the hunter played is a menu only with no hunter of a quest (`weapon_hunter`): a ballista or a cannon aims
  without the hunt camera's handler, and its stick became the d-pad (#169; not yet seen on one: the touch log names the action
  and whether the hunt camera runs each time the hunter is played or not).
- The renderer's `vk*` are volk's pointers (`third_party/volk`, force-included by `native/CMakeLists.txt`), filled from `runtime/platform/android_gpu.c`: the
  system's `libvulkan.so`, or the player's driver through `third_party/libadrenotools`. `volkLoadDevice` is not used, so calls go through the same loader
  entry points as before. A trial file sends a driver that never drew back to the system's. The hook libraries must stay extracted (`extractNativeLibs`).
- Custom drivers, tested on an Adreno 830 with a marked copy of its own library, not yet with Turnip. A library that can't be opened doesn't stop the game:
  `hook_impl` falls back to the system's, and so does one named `vulkan.adreno.so` (the system's, already loaded); `gpu_driver_check` notices
  both (below). `AdrenoVK: Driver Path` in logcat tells which one runs. A driver that dies before drawing goes through the trial file.
- Adreno 630 on Android 10 (driver 1.1.87): SIGSEGV in `vulkan.sdm845.so+1dd318`, from `vkCmdClearAttachments`. Drivers under 1.1.128 clear with draws
  (`clears_drawn`, `MHGU_CLEAR_FORCE=1` tests it anywhere). Adreno with Vulkan 1.1: below 720 lines and without MSAA the 3D picture repeats in a grid, so
  `msaa_floor` keeps 2x unless `[Performance] AdrenoFix = Off` (an Adreno 610 on 1.1.128 looked dark and flickered with it). Its cause is unknown.
  The one picture of it (#12: Adreno 710, 1.1.128, 894x402, Low-end off) shows the whole scene shrunk into tiles of about 160x128 of the
  3D picture's pixels, the HUD right: the size of a tiler's bins, a lead for whoever has such a phone. On Android a 3D picture 704 to
  719 lines tall is made 720 (`config.c`, any GPU): 66% of a 1080-line screen gave 1582x712, which got the 2x for those 8 lines
  (a Galaxy M55's Adreno 644: 15-20 FPS, against 30 at 1600x720 with no MSAA; #190).
- On the Adreno 616 and 630 drivers of Android 10 (1.1.87) D32_SFLOAT_S8_UINT can't be sampled or copied into (one of the two), and D24S8
  lacked something asked of it (blits, most likely). Up to 0.1.8 DEPTH24_STENCIL8 became D32S8 there and `create_image` then made the
  game's depth buffer RGBA8 ("format 130 (nvn 0x35) not supported: RGBA8 instead"): the device was lost at the first 3D scene after the
  opening movie, character creation (#22, #40, #61, #124). Every log of that crash has the line; the Adreno 618 on the same driver,
  which gets past it, hasn't. Not seen on a device here. `depth24_stencil8` now weighs what each format can do (its features are in
  the log line), `depth_format` never turns a depth format into a colour one, an image asks only for the usage its format has, and
  depth copies, blits and clears the format can't do are left out (`copy_texture`, the MSAA depth resolve, the snapshot of a depth target
  read while drawn into, the HUD's depth clear: `depth_copy_ok`).
  `MHGU_VK_FMT_MASK=<VkFormat>:<hex features>` takes features away to test those paths on any GPU.
- A lost device (`VK_ERROR_DEVICE_LOST` at a submit or a wait) logs every submission not known finished: its passes (size, first colour
  format, depth format, samples, draws), the programs drawn (vertex / fragment code address, a line per pass, 160 at most: the game's
  code addresses, the same on every device, so a dump can be compared with a test phone's), drawn clears and copies, then the GPU
  driver's own lines from the app's logcat (`lost_report`, `exec.c`). The submit that reports it is often not the one that faulted,
  so the last 4 known finished are listed too (`(finished)`): Adreno's KGSL retires a batch that faulted, its fence signalled, and
  refuses the next submission with EPROTO (`IOCTL_KGSL_GPU_COMMAND failed: errno 71`, "the context faulted before"; EDEADLK once it
  gives the context up). 0.1.9.1's report named only that next one: an empty submission (#191). `devicelost` in the control file
  fakes one. Each kind of depth copy or blit is named once in the log where the game first does it (`[vk] depth blit ...`,
  `depth_copy_note`).
- Adreno 618, Android 10 (1.1.87, Xiaomi surya; #138, #140, #160, #191, one player): the GPU faults the moment the hunter walks
  from Verdant Hills' area 4 into area 5 (a cave), every time, 0.1.6 to 0.1.9.1, 30 or 40 FPS, Shadows on or off; 3D 790x356
  (33 %), Low-end mode, MSAA 1x, FXAA off. Both 0.1.9.1 crashes came at submission 15203. No depth copy or blit is new there,
  and the vertex cache was under its 384 MB. Not the cause, checked on the RG406V (Mali-G57) with the validation layer, the
  Adreno paths (`MHGU_VK_ADRENO=1 MHGU_VK11=1 MHGU_CLEAR_FORCE=1 MHGU_VK_D24=0`, no int64, divisor or border colours), the
  player's settings and size (`MHGU_RENDER=792x356 MHGU_ASPECT=2400:1080`) and the quest started in area 5: no validation
  error, the same samplers and formats as area 4 (the depth target sampled nearest), no program with subgroup operations
  (that driver has them in compute only). Not tried: walking from 4 into 5 under the validation layer.
- A romfs under 12.3 GB with all 35,531 files is an incomplete copy: a black picture, then `no function at 0`, or a game that closes in one
  area. None of the game's files is empty. The game's own folder is measured, before the mods (a mod's files, smaller or larger, moved
  the merged image's size either way), and without `nativeNX/movie` (1.39 of the 12.31 GB: copies with the movies encoded again, 11.0
  GB, play right, #171): under 10.65 GB of the other 10.92, or with empty files (mods' aside), the log warns and, a few seconds in, a
  notice tells the player (`romfs_incomplete`, `overlay.cpp romfs_notice`; `MHGU_ROMFS_SHORT=11.5` shows it on a complete copy). The
  log's `[romfs] the game's own files` line gives both sizes, and a file or folder the scan can't read (`stat` or `opendir`
  refused) is named, the first five.
- `Config` gets its defaults by position: `CONFIG_DEFAULT_VALUES` in `config.c`. A new field in `config.h` needs its value there, in its
  place, or every later field moves (two added once turned most touch buttons off).
- The shadow maps are one 1024x3072 depth atlas (three cascades), the first pass of a hunt's frame: 47 draws, 2.8 ms of a Moto g06's 18.6.
  The game clears it with three full-size draws, not a clear. `[Performance] Shadows = Off` clears it once and skips its pass (`shadow_skipped`).
  The game makes a new atlas at every town and quest load: which are cleared is kept in each one's TexBackend (`shadow_off_gen`). A list
  of the first 8 left the 9th and later uncleared, the scene dark after a few quests (#165; checked on the Moto g06 through 10 loads).
  In the Deserted Island's camp (quest 301, a dump on an RX 6800 XT) it holds only the hunters, cats and small props; the scenery's
  shadows there are not in it, and the mask pass after the depth pre-pass (blue only) draws only those and the ground right under them.
  Its casters: polygon offset factor 3, units 0, depth clamp on. The log says once each offset a depth-only pass gets (`[vk] depth-only
  pass`; the pre-pass at the 3D size gets units -96 to -512; on D32S8, AMD's stand-in for D24S8, a unit is 2^(e-23), e the exponent of
  the primitive's largest depth: D24S8's 2^-24 for depths from 0.5 to 1, half or less below 0.5) and any 3-component vertex format read
  with 4 (`[vk] vertex format`, none on that card). #180's flicker in area 6 is not seen in the camp.
- The chat's log is a layer 97 window at the bottom left (x 30..605, y 547..708 of the 1280x720 canvas) that stays seconds after a line: it
  must not count as a menu (`touch_hud_menu`), or the menu pad shows over the hunt.
- A model's skeleton: +0x490 the joint count, +0x494 the array, 0xa0 bytes each, world position at +0x40 (w 1.0), parent at +0x91 (0xff the
  root). The root is the pelvis. Locked shots aim there (`mousecam.c body_center`).
- `camera_fovy` compares the camera's aspect with the screen's with a tolerance: a small 3D picture's rounding made them differ at 33%.
- DLC: the game asks `aoc:u` only from its main menu's Download, which needs its online services and stops with an error without them,
  even with `MHGU_FAKE_NET=1`. `services/dlc.c`, the RomFS images and the importer are in place; the menu row shows with `MHGU_DLC=1`.
- Cheats, mods and DLC are read from the game folder's `cheats/`, `mods/` and `dlc/` too (a file manager reaches it, not `Android/data`).
  `ImportActivity` copies there; imported cheats start off (`cheats/_off.txt` in the app's folder).
- Cheats as the community's lists come (`runtime/cheats.c`, 1.0): an end of a block with none open is passed over and a stray else
  skips to the next end, as Atmosphere's VM does (such cheats were left out: "an end of block without its start"); words written
  together (16 or 24 digits) are split 8 by 8; a bad word is named in the log. A file named by another build id than 1.4.0's
  (FB08F1D20FD1204F) is another version's and is left out (`cheats_left_out`, a row in the Cheats tab). A mod's `cheats/` folder
  (yuzu's and Ryujinx's layout) is read under `<mod>/<file>`, its cheats off until turned on (`+<mod>/<file>` lines in
  `_off.txt`); the import screens take a mod folder that has only `cheats/`.
- Code mods beyond IPS / pchtxt patches (1.0): a mod's whole `exefs/main` (or `sdk`) laid out as the game's is compared with the
  game's and its differences applied as patches (`loader.c replace_from_mod`; another layout is left out, the Mods tab marks the
  mod with `!`); bytes a patch puts after the code (the room up to `.rodata`, a "code cave") are interpreted code; a jump into the
  middle of a game function no patch touched (a cave jumping back, a patched jump table) makes that function interpreted
  (`dispatch_interpret_inside`, in `dispatch_slow` and `interp_run`) instead of FATAL "no function at"; a Forge plugin's code
  patch that is not an entry hook makes the functions it changes interpreted (it was ignored), and `forge_mem_getMainRoDataAddr`
  (newer plugins' name) is provided. Test mods made from a local `main` (never committed): a call in the frame update redirected
  through a cave into the callee's middle, and a whole `main` with one instruction swapped for an equivalent one.
- Forge's menu without a keyboard (S44): Mods tab, "Forge menu (plugins)" (shown when a plugin started; `MHGU_FORGE_MENU=1` shows it
  without plugins, for tests) opens it once the settings menu has closed (`overlay_open_forge`). Opened by touch it is drawn 2
  times bigger (font and spacing, the plugins' free drawing untouched), placed in the middle each time, scrolled by a finger's
  up or down drag from anywhere (a pressed header or button is let go, `ClearActiveID`; a slider the press already changed keeps
  the finger; the hover test allows a blocked window, or a pressed control hid it: `frame_once`), moved only by its title bar;
  its x or the phone's Back closes it; a field that takes text brings up the phone's keyboard (`textinput.c` state 4). Tested on
  the PC with the control file's `finger` lines (touch layout, row, window, x).
- Every minute the log has a `[mem]` line: memory map entries, resident memory (own, the files mapped, shared) and VMA's blocks, what
  is allocated in them and by what (images, of those the BC textures decoded on the CPU, the vertex cache, the staging ring).
  `MHGU_MEM_LOG=<seconds>` writes it more often. Games on PowerVR closed after 5 to 15 minutes out of map entries; there VMA is told
  Vulkan 1.0 so images share its blocks (`MHGU_VMA_DEDICATED=1`: the old way).
- PowerVR's shader compiler crashed building several pipelines at once (#73, #99): there the builds run on one worker and every call
  into the driver's compiler takes one lock, from any thread (`one_compiler_install`, `device.c`: volk's `vkCreateShaderModule`,
  `vkCreateGraphicsPipelines`, `vkCreateComputePipelines` and `vkGetPipelineCacheData` replaced; the `[mem]` line counts them; FSR's
  backend, which looks them up itself, gets the same through `vk_device_proc_addr`). Not on the
  render thread: 0.1.8 did that, and the start built every saved program and pipeline before its first picture, a black screen on a
  Helio G35 (#157). `MHGU_VK_POWERVR=1` gives any GPU what a PowerVR gets (this, no prewarm, VMA as Vulkan 1.0). The log's `[shader]
  the builds queued at the start are done` line says how long the start's builds took (Moto g06, warm caches: 1849 in 2.6 s) and,
  where the driver has VK_EXT_pipeline_creation_feedback, how many of the pipelines its cache had; the start's `[vk] the driver's
  pipeline cache: N KB from the last run` says what was read. Players' Mali phones on the Moto's own driver took 84.5 s (#199, Helio G85)
  and 86.8 s (Discord, Unisoc T7250, whose session before had ended in a crash) there: their driver cache was cold or incomplete.
  `cache/vk_pipelines.bin` used to be written only in the background and at exit, and a run that ended otherwise (a crash, a lost
  device, the process killed; perhaps a write cut short by a quick swipe from Recents: not seen) kept nothing of it. A compile worker
  now also writes it once the start's builds are done and then at most every 2 minutes while 64 or more new pipelines came
  (`pcache_autosave`, exec.c), and the render thread queues that save when a burst ended inside the 2 minutes (`pcache_tick`: the
  workers only look after a job, and a town's and a quest's pipelines, built in the minute after the start's were kept, waited for
  the next build). The PC, a killed run: 0 of 933 from the cache at the next start, with the autosave 933 of 933. The Moto g06
  (MHGU_HOME with the program cache but no driver cache, quest 101, killed each time): the start's 152 pipelines 15.6 s, then 0.2 s
  from the 1663 KB kept; the quest's pipelines lost without `pcache_tick` (next start 178 of 308 from the cache, 16.4 s), kept
  with it (3313 KB 2 minutes in, next start 308 of 308, 0.4 s). A save asks the driver again with a quarter more and 1 MB of room
  (64 KB before) and logs what it wrote. The creation feedback always gives the stages their array too
  (`pipelineStageCreationFeedbackCount` = `stageCount`): with a count of 0 and no array, the RG406V's Mali-G57 driver (Vulkan
  1.3.225) wrote the stages' feedback through the null pointer, a SIGSEGV at 0 in libGLES_mali.so on every compile worker at
  every start (found when the 0.1.9.3 builds were installed; the Moto's driver took it).
  A crash inside the compiler itself (#100: a GE8322's SIGSEGV in `libufwriter.so`, on the worker in `build_pipeline`, while
  the game waited in the settings) adds to the crash report the program of the build under way (`building_note` through
  `rt_fault_note`: "this thread was building a pipeline in the GPU driver: the program of code <vertex> and <fragment>"),
  so the next log names it. `MHGU_TEST_PIPECRASH=<n>` (a test, with `MHGU_VK_POWERVR=1`) faults at the n-th build.
- Phones give memory back (`staging_trim`, `vc_trim`, `exec.c`): a staging chunk the ring hasn't taken for 8 s, and vertex cache ranges no
  draw used for 20 s. Before, both only grew: the chunks a burst of uploads made (an area's textures, decoded on a Mali) and the
  vertex data of every area visited, up to the cache's 384 MB, which a 2.8 GB phone couldn't hold (#59, #156). The vertex cache is
  192 MB at 4 GB of memory or less, 128 at 3 GB. That is what it keeps, not a wall: when the least recently used range was drawn
  from in the last 4 frames (`VC_HOT_FRAMES`), so all were, it goes over it up to `vc_ceiling` (twice it, 384 MB at most), past that
  a draw copies what it reads, and `vc_trim` drops what is no longer drawn from to get back under it. Before (0.1.9, 0.1.9.1) it
  dropped ranges in use and copied them whole again: a town's menus (the Palico info: the town and the menu's models, 173 MB) on
  the Moto with `MHGU_RAM_MB=2800 MHGU_LOWMEM_MB=4000` copied 1.4 GB a second, the translating thread 13 -> 35 ms, 30 -> 16-26 FPS
  (12 FPS at the item box on a 2.8 GB Infinix), and the staging ring grew to 160 MB carrying those copies; now 30 FPS, 14.5 ms, VMA
  364 MB instead of 524 there. `MHGU_VK_STATS`' `[vk] 2s copied` line gives the bytes held and the draws past the ceiling.
  Under 350 MB of the system's memory available (`memory_short`: Android 14 and newer
  no longer tell a foreground app memory runs low) both wait 2 s instead. `MHGU_LOWMEM_MB=<n>` sets that threshold;
  `MHGU_VK_STAGING_TRIM=0`, `MHGU_VK_VC_TRIM=0`, `MHGU_VK_VC_MB=<n>`: as before;
  `MHGU_RAM_MB=<n>` takes the device's memory as that much (the texture and cache choices of a smaller phone).
  Measured on the Moto g06 (3.7 GB, Mali-G52), `MHGU_QUICKHUNT=101`, three minutes into the quest: VMA's memory 540 MB with 0.1.8's
  behaviour (the three variables above; a 365 MB vertex cache), 380 MB now, 348 MB with `MHGU_RAM_MB=2800` and the memory short;
  `dumpsys meminfo` GL mtrack 631, 500 and 453 MB, total PSS 1377, 1273 and 1241 MB; the same frame rate. A staging chunk is a VMA
  allocation of its own and goes at once, but a vertex cache range shares VMA's 32 MB blocks with images: memory goes back only
  when a whole block empties (56 MB of ranges dropped gave back one block). A pool of its own for the cache would give more back.
- A submission the driver refuses for memory (`vkQueueSubmit` -2 or -1) is made again (`submit_again`, `exec.c`): nothing of it ran
  and the spec keeps what it names as it was, so after the GPU's work in flight ends, the objects waiting for it go and every idle
  staging chunk and vertex cache range goes back at once (`trim_now`), it is submitted again, up to 4 tries in about a second; still
  refused, the error as before. A 2.7 GB phone with a PowerVR GE8320 closed so at a potion, an attack, the smithy or a quest's start
  with the system's memory at 150-450 MB (#201, #139; its GPU memory was 250-410 MB, 928 MB before 0.1.8's 32 MB blocks): whether
  a second try is taken there is unproven (no such phone here). `submitoom <n>` in `control.txt` refuses the next n tries (a test:
  on the PC, 1 then 3 refusals: taken at the 1st and the 3rd try again; 5: the error as before; on the Moto g06 the trims gave back a
  staging chunk and 34 MB of vertex cache, then "taken"). The stand-in, on the Moto with `MHGU_TEST_IMAGE_MAX=512`: 20 stand-ins
  at the start, the game went on.
- A texture whose size no image of the GPU can have (an NVN texture made from garbage: "256x2396769997x1519567012", a cube array,
  entering area 4 of map m01d on a Mali-G57 with a Vulkan 1.1 driver, #198: out of host memory, then the RGBA8 try's error stopped the game), or one
  no image could be made for at all, gets a 1x1 stand-in (`stand_in`, `textures.c`): never uploaded (its guest memory isn't read),
  copied or rendered into, sampled as the dummy of the shader's kind. `MHGU_TEST_IMAGE_MAX=<n>` (a test) takes the GPU's images as
  no wider or taller than n.
- A player's driver is checked once the device is made (`gpu_driver_check`): no library from its folder loaded means the system's runs,
  and the choice moves to `failed`. `DriverActivity` renames a library named as the system's (`vulkan.adreno.so`) to `vulkan.player.so`.
- The Khronos validation layer: put `libVkLayer_khronos_validation.so` (Android binaries of Vulkan-ValidationLayers) in `jniLibs` for a
  test build only, and `MHGU_VK_VALIDATION=1`. Never in a public APK.
- `minSdk` is 28 (Android 9). `./gradlew lintRelease` lists calls newer than that (`NewApi`); `native/build_native.sh` builds for API 28.
- Built for API 28, every `__thread` variable is emulated TLS: a call to `__emutls_get_address` and a `pthread_getspecific` per access.
  `rt_dispatch`'s cache was one, on every indirect jump of the game (a tenth of a game thread on a Moto g06): it is now per `Cpu`, through a
  pointer in `Cpu._pad` (`dispatch.c`, bytes the game's code never touches; `cpu.h` itself must not change). Keep `__thread` out of hot paths.
  That cache never kept HLE functions, so every call of one went through `dispatch_slow` (PC, quest 101's camp at 30 FPS: 266,000 a
  second on the render thread, its NVN calls, and 56,000 on the update thread), and addresses 16 KB apart evict each other, each
  miss a binary search over all the game's functions (2,500 a second on the update thread). `MHGU_PERF_DISPATCH=1` (0.1.9's perf
  pass, off: measured without a change) keeps the hit path and changes the miss: a second way of 4096 entries indexed by a hash (a hit there
  swaps the two), HLE functions cached too (their `rt_unwind_point` does nothing: nothing sets `g_rt_unwind`; a reboot that sets
  it would have to empty the caches), and a lookup searches only its 4 KB page's functions (`page_first`, the same lower bound).
  The same function for each address; then about 2,700 misses a second on the update thread, all found in the second way.
  The RG406V at Low-end 60 in the camp: before, 316,000 HLE calls a second through `dispatch_slow` on the render thread and
  64,000 plus 3,900 binary searches on the update thread; with it, 4,500 misses a second on the update thread, all in the
  second way, and the render thread's too few to log.
  `MHGU_PERF_DISPATCH_STATS=1` logs each thread's misses every 10 s, either way.
- Locks and write tracking, counted (`MHGU_HITCHLOG=1`, `MHGU_WT_STATS=1`), the RG406V at Low-end 60 in quest 101's camp: the
  renderer's lock waited for 3 times in 2 minutes, vkAcquireNextImageKHR 0.08 ms a frame, no wait for the frames in flight,
  1.8 write faults a second (2 blocks armed a second, 4,500 queries): nothing there to gain.
- Profiling: a test APK with `<profileable android:shell="true"/>` (put it in, build, take it out), NDK `simpleperf record --app` on the
  device, `binary_cache_builder.py -lib native/build/arm64-v8a`. `--call-graph fp` stops at the Mali driver; `dwarf` gets through. The
  stripped Mali library shows its own code as `stdlibp_neon_f32x16_to_f16x16`: that is the driver recording draws, not a conversion.
- The audio renderer had 70 effect slots of 1.7 KB, searched for every order number on every mix: over half the audio thread. `fx_order`
  lists them once after an update (`services/audio.c`). `getenv` walks the whole environment: never call it per frame, cache it.
  MHGU has 1152 splitter destinations and every frame copied their 96 bytes of volumes (prev <- vol), 200 times a second: a fifth
  of the mixer in a Moto g06 profile. `MHGU_PERF_AUDIO_SDEST=1` (0.1.9's perf pass, on by default) copies only those an
  update changed (`sd_dirty`; in a quest about 1 in 6600); `MHGU_PERF_AUDIO_ADPCM=1` decodes DSP ADPCM a 14-sample frame at a
  time (`adpcm_block`, the same integer steps). Both give the same samples bit for bit: `MHGU_PERF_AUDIO_CHECK=1` compares them
  with the old way as the game runs and logs any difference every 10 s (PC, quest 101: 100 s, 5.6 million ADPCM samples,
  none; the RG406V: 6 million, none; the decoder's fuzz test, 400 million samples on x86-64 and on the RG406V, none). The
  RG406V at Low-end 60 in quest 101's camp, standing (93 s each, on the charger): the mixer thread 0.16 CPU-s a second, 0.13
  to 0.14 with both; the process 119 CPU-s a minute, 116 to 117.
- The guest's `SleepThread` sleeps precisely (`plat_sleep_until_ns`: a 1 us timer slack set for good on the thread, the last 50 us
  spun with `sched_yield`), and the game polls with it. The SDK's SleepThread (0x24a31c4) is a leaf, so at the SVC lr is the game's
  call of main's stub 0x15efdb8 (`kernel.c` `sleep_call_site`). `MHGU_SLEEP_LOG=1` logs each call site's calls every 10 s. PC, quest
  101's camp at 30 FPS: 0xdffcac 190 a second (5 ms each, one thread), 0xdffcd8 30-44 (20 ms), the frame limiter 0xda5d2c 30, 0x5cd704
  about 18 (50 ms), the SDK's own 10 (100 ms); the job loop's 100 us retries for a job's dependency (0xdc7bb8) and a busy lock's 1 ms
  backoffs (0x1431030, 0x143108c) did not show there. `MHGU_PERF_SLEEP_COARSE=1` (Android, 0.1.9's perf pass, on by default):
  the game's own calls but the limiter's, and the audio mixer's 200 Hz tick (its queue holds about 45 ms), sleep once to their time
  with the thread's timer slack as it is (the system's 50 us on a thread that never slept precisely) and no spin
  (`plat_sleep_coarse_until_ns`), some tens of microseconds later; the SDK's own sleeps and the vsync tick stay precise.
  The RG406V at Low-end 60 in the camp: the same sites and rates (0xdffcac 188 a second); coarse, a 5 ms sleep took 5.18 ms
  instead of 5.06, and the process's CPU time moved less than the noise (118.3 CPU-s a minute against 118.7 and 119.1).
- `gpuprof` shows the window image's pass (the game's picture copied in, the overlay) as its last line. The game's own passes all load
  their attachments; one whose first commands clear an attachment whole begins with `LOAD_OP_CLEAR` and those values instead
  (`noload_clears`, `render_pass`), and the clears it already did are skipped (`pass_skip_clear`). Not `DONT_CARE` plus the clears: an
  Adreno 830 then drew the scene black but for some tiles. Anbernic (Mali-G57) GPU frame -6 %, Adreno 830 -0.7 %. `MHGU_VK_NOLOAD=0`: off.
- Render pass power switches (0.1.9's performance pass): `RODEPTH`, `SHADOW_CLEAR` (2) and `FXAA_COPYDRAW` on by default,
  `HUDSTART_FUSE` 3, `FMTLIST` off; `MHGU_PERF_<NAME>` in the environment (`env.txt`) overrides each, read once at start and named in the log (`[perf]`); with `MHGU_VK_STATS=1` a `[perf] 2s` line counts what they did.
  - `MHGU_PERF_RODEPTH=1`: a draw that samples its pass's own depth target without writing depth or stencil (the two after the
    scene's pass, every frame) reads it where it is, the pass begun again with that target `DEPTH_STENCIL_READ_ONLY_OPTIMAL`
    (`RP_RO_DEPTH`), instead of a copy of it (`resolve_feedback`'s snapshot); a later depth or stencil write ends such a pass. Not
    with MSAA or an FSR stand-in. RX 6800 XT: 240 snapshots in 2 s down to 120 (the other is a colour one).
  - `MHGU_PERF_SHADOW_CLEAR=1|2` (Shadows On): the atlas's pass reads last frame's 12.6 MB back in, though each cascade begins with a
    draw writing one depth over all of it. An atlas's first frame begins it filled with a marker depth and reads each such draw's
    rectangle back (`at_check`): every texel the same value, never the marker, the rectangles the whole atlas. From then on its pass
    begins cleared to that value, in frames whose commands, read ahead (`at_look`), have those draws again; each is compared as it
    comes (`at_sig`: programs, vertices, constants, state), and one that differs turns it off for that atlas (logged). `=2` also leaves
    such a draw out where nothing was drawn over its rectangle since (`at_mark`, 32x32 tiles). RX 6800 XT, quest 101: three draws
    writing 1.0, all three left out every frame, the atlas's dump the same as without.
  - `MHGU_PERF_HUDSTART_FUSE=1|2|3` (plain scaling, Low-end mode), bits: 1, the HUD's output-size depth stand-in is cleared as the
    first pass that renders into it begins (`LOAD_OP_CLEAR`, far and 0 as before) instead of by a clear of its own that pass then
    read back in (`fsr.c hud_start`, `vk_depth_clear_later`); 2, H is scaled into H' as that pass's first draw (`fill_draw`, a
    full-screen triangle sampling H bilinearly at each pixel's centre) instead of by a blit before it, once the first frame's draw
    and blit are read back and give the same bytes on that GPU (`fill_check`, logged; else the blit stays). Anything that reads
    H' or that depth target first, or a pass that doesn't cover it whole, gets the old blit or clear first. RX 6800 XT, 50 %:
    806x360 -> 1612x720, the same bytes.
  - `MHGU_PERF_FXAA_COPYDRAW=1` (FXAA Off, not with FSR or plain scaling): `fxaa_skipped`'s copy of the FXAA pass's input into its
    target is drawn as the first draw of the next pass when that pass draws into the target (with Bloom On the game adds the
    bloom there next), its target begun cleared under it, instead of a copy that pass then read back in; checked once against
    the copy as bit 2 above (`copy.frag`, each texel fetched). Any other pass, copy or dispatch first gets the copy. With Bloom
    Off the next pass draws into the input instead, so nothing changes. RX 6800 XT, 100 %: 1612x720, the same bytes.
  - `MHGU_PERF_FMTLIST`: set at all, the log says what the driver makes of `VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT` (`fmt_probe`: an RGBA8
    image's memory with it, with a format list, without it; a compressed image needs more, its metadata) and names each image / view
    format pair the game asks for. `=1`: images no view of the game reads as another format (decoded BCn and ASTC, widened, fallback
    formats) get their own format and its sRGB / UNORM twin as their list, or no flag. RX 6800 XT: 4194304 bytes for a 1024x1024
    target with the flag, 4211056 with a list or without it (AMD's colour compression: off with the bare flag).
  Left out, with why: Shadows Off skipping the shadow mask's pass (its draws write the scene's depth, and what they write depends on
  each frame's receivers, so no check proves a frame's draws change nothing); FXAA Off sampling its input in place of its target
  (the target is drawn into right after, over a load, or its input is drawn into while the target is read: hence the copy drawn
  in that next pass instead).
- Descriptor sets: `stage_set` reuses one written earlier in the same submission when every write is the same (about a quarter of the
  sets), and draw skips binding the same two again (`SS_SETS`; any other graphics bind forgets them). Zeros, the identity rescale table,
  the alpha reference and each stage's driver constants are copied once per submission (`shared_*`, `zero_ubo`), or every set would be
  new; a storage buffer of zeros is shared only by shaders without `uses_gmem_store`. `MHGU_VK_SETREUSE=0`, `MHGU_VK_SHARED=0`: off.
- The translator's performance switches, on by default since 0.1.9's performance pass (`MHGU_PERF_<NAME>=0` in `env.txt` or the environment, read
  once at the start; the log's `[shader] translator switches` line says which are on). The program cache's stamp includes them, so a run
  with others translates every program again: `MHGU_NO_PROGRAM_CACHE=1` in both arms of an A/B. `MHGU_PERF_NMINMAX`: fmin, fmax and
  saturate as GLSL.std.450's NMin, NMax and NClamp (`glsl_prelude_nminmax`; Vulkan 1.2 drivers but a PowerVR's), Maxwell's NaN rules in
  one instruction each instead of compares and selects. `MHGU_PERF_IPA`: where a shader multiplies a varying by w and by 1 / w (an
  IPA.MULTIPLY by the MUFU.RCP of position w's IPA, `ipa_w_track`) the varying is read as it comes, 1704 of the 1736 such reads of the
  quest-101 camp's 211 fragment shaders; not bit-exact (the old product was 1-2 ulp off the interpolated value). `MHGU_PERF_VSTRIM`: a
  vertex shader's outputs its fragment shader doesn't read become plain variables (`maxwell_trim_outputs`; 771 of the camp's 1381),
  dead code to the driver's compiler. Checked on an RX 6800 XT with the scene held still (a test build's hook: the engine's frame step
  near 0) and each switch's programs translated anew in the same run, shot between two shots with the switches off: NMINMAX and VSTRIM
  change no more pixels than translating anew with them off does (9 and 44 against 10 and 45 of 921,600, at the noise's 1-6 / 255), IPA
  0.14 % of them, single pixels scattered over textured ground and rock, most by 1-4 / 255 (at most 32). The old functions against
  NMin / NMax / NClamp over every pair of special values (+-0, +-inf, NaNs, denormals) on the GPU: the same bits on a Mali-G52 MC2 and a
  Mali-G57 but NaN payloads; the RX 6800 XT also flushes a denormal returned beside a NaN. The RG406V (Mali-G57, 100 %, no frame cap),
  the same held scene, which stands fully still there: NMINMAX and VSTRIM change no pixel, IPA 0.17 % of them by 1-7 / 255; `gpuprof`,
  four frames of each between frames with the switches off (4.5-4.7 ms main pass, 21.4-22.1 ms GPU frame): the main pass 4.25 ms
  with NMINMAX, 4.37 with IPA, 4.48 with VSTRIM, 3.78 with the three, the frame 20.9, 21.4, 22.1 and 20.1. The shadow atlas, whose
  vertex outputs VSTRIM all leaves out, didn't move: that driver may already drop them, or the pass isn't held up there.
- The translator's per-draw work has test switches (0.1.9's performance pass, `nvn/vulkan/exec.c` `perf_init`), each read once at
  start and named in the log's `[perf] draws` line, all on by default (`=0` in `env.txt`: the old way): `MHGU_PERF_GETENV` (debug
  switches read once, not by `getenv` at every depth clamp command), `MHGU_PERF_DRVTAB` (the driver constants' 256-byte storage
  buffer table compared before their 2 KB are zeroed), `MHGU_PERF_TEXSCAN` (a draw's textures looked up once for the movie, MSAA
  and feedback checks; the MSAA one skipped until a pass rendered multisampled), `MHGU_PERF_TEXTIME` (a texture bind with nothing
  to upload isn't timed), `MHGU_PERF_PIPEMEMO` (a draw with the last draw's pipeline key takes its pipeline without the hash),
  `MHGU_PERF_LAZYIDX` (a draw's largest index found only when a stream's copy needs it: none at a camp) and `MHGU_PERF_IDXCOPY`
  (16 / 32-bit indices copied once per submission per range, as uniform buffers are: the same mesh in the shadow, depth, mask and
  main passes); `MHGU_PERF_DRAWS=0` turns all seven off (a switch of its own still wins). `MHGU_PERF_DYNUBO=1` binds uniform buffers with dynamic offsets (the game's
  per-object ones first, then the compiler's and the driver's constants, up to the GPU's `maxDescriptorSetUniformBuffersDynamic`
  a program, logged: 8 on an RX 6800 XT), so a copy's place in the staging ring is no longer part of the set and `stage_set` reuses
  most sets (quest 101's camp on the RX 6800 XT: 28 % reused before, 69 % with it); it changes the programs' pipeline layouts, so
  the driver's pipeline cache is cold once (every pipeline built again at the first start with it: warm up before measuring).
  `MHGU_PERF_CHECK=1` does the old work beside each cut that is on and counts what differs (`[perf] 2s check`; 0 in every run so
  far); with `MHGU_VK_STATS=1` a `[perf] 2s` line says what the switches did.
- Texture uploads (`nvn/vulkan/textures.c`): a texture's first upload is hashed and copied into staging memory by three worker
  threads in bands of about 1 MB, and the submission waits for them (`vk_upload_jobs_finish`): nothing draws early or half uploaded.
  The list's own CPU-side writes (ClearBuffer, CopyBufferToBuffer, UpdateUniformBuffer, ReportCounter) run at once, so one over
  memory a queued band has yet to read waits for the bands first (`vk_upload_jobs_guard`, `tex-guard` in the hitch log): a draw
  before the write sees the old contents, as on the console's GPU. What each texture's queued bands read is kept until they are
  done (`up_range`); a write over none of it waits for nothing.
  Under 256 KB, and formats decoded on the CPU (BCn on a Mali, ASTC), stay on the render thread. `tegra.c` copies a GOB whole (into
  staging memory 5 to 9 times the old speed). On a desktop the staging ring's next 32 MB chunk is made ahead on its own thread: made on
  the spot it cost 34 ms inside a frame. `MHGU_TEX_ASYNC=0`, `MHGU_VK_SPARE_CHUNK=0`: off. A texture that changes on 120 frames in a
  row is named once in the log (`[tex] changed and uploaded ...`).
- The safety nets, in case the page write tracking missed a write: the vertex cache copies each range in use again every 2-4 s,
  2 MB a frame (Android; off on Windows: `MHGU_VK_VC_SAFETY=1` turns it on), and a texture is hashed whole every 120 frames, 4 MB a
  frame (every second at 120 FPS). `MHGU_VK_VC_SAFETY=2` and `MHGU_TEX_NET_CHECK=1` (every texture every 2 s, any size) check the
  tracking: a change with no write reported is logged, with counts (`[vk] vertex cache check`, `[tex] check`; a texture's is told
  apart from a write reported that the sampling hash `fnv_sample` didn't see). `MHGU_VK_STATS`' `[vk] 2s copied` line splits the
  copies and the hashing by cause. On by default (`=0`: the old way): `MHGU_PERF_VCNET=1` (the vertex cache's every 30 s, 512 KB a frame),
  `MHGU_PERF_TEXNET=1` (a texture's 30 s after its last whole hash on Android, 4 s on a desktop, 32 MB a second),
  `MHGU_PERF_TEXSAMPLE=1` (the sampling hash only where it is compared: the same decisions) and `MHGU_PERF_TEXSTREAM=1` (a
  texture changed at each of its last 4 checks is copied at once when written, hashed as it is copied: a hunt has four 1 MB
  RGBA32F 1024x64 textures, one written a frame, hashed and then copied). The checks found no write missed: on the RG406V
  (Mali-G57) standing at the camps of quests 101, 302 and 630 (18 min, three town and quest loads) 31446 whole texture hashes
  with no write reported and 11095 vertex cache slices, none changed; on the PC 14422 and 8000. Standing at the quest-101 camp
  (ABBA, 200 s runs), the four on: the RG406V at its 60 FPS, the translating thread's exec 668 -> 622 ms per 2 s, `uploads` 64
  -> 12, copied 218 -> 161 MB; the RX 6800 XT PC at 60 FPS, the vertex cache's net on in both, 298 -> 269, 28 -> 11, 170 -> 94.
- A Galaxy (Samsung) refuses every simpleperf event for apps (`cpu-clock`, `task-clock`, `cpu-cycles`): profile the CPU on another
  phone. Its frame is CPU-bound: the GPU span in `gpuprof` includes the gaps where the GPU waits for the render thread.
- `[Performance] LowEnd` is Off, On or Toaster (2). On a phone (a desktop's: Building for Windows) everything that tests `low_end`
  treats Toaster as On; Toaster adds a HUD at most 540 lines tall and smaller BC textures, and the menu sets 25 %, 30 FPS, Shadows,
  Bloom, Reverb and Grass off (leaving Toaster gives the Reverb and the Grass back, as leaving Low-end does the rest).
  `[Performance] Bloom` (the bloom and depth of field chain, `lowend_skipped`; not with real FSR: the plain scaling sets `g_fsr_on`
  too, so test `g_config.fsr`) and `[Audio] Reverb` are switches for everyone. A file with LowEnd on and no Bloom key gets Bloom off,
  as before. Toaster's HUD can be smaller than the 3D picture (one over 540 lines: 66 % of a 1080-line screen), so the stand-ins are
  then smaller than what they stand for (`pass_sx` below 1): a rectangle already in the stand-in's size must not be scaled again
  (`scissor_rect`: the item bar's relaxed scissor was, and the bar was cut off past 75 % of the screen; the RG406V shows it at 80-90 %).
- The game tests its markers over the world (speech bubbles, quest icons) in the 1280x720 HUD canvas against the view's viewport
  in pixels (main 0x218dcc): with a 3D picture smaller than 1280x720 those past its size were hidden (#188; 26 % of 1600x720 hid
  the middle of the screen). `runtime/hle/guimark.c` does that test with the viewport never smaller than the canvas. To see one
  without walking: `MHGU_QUICKHUNT=town`, then order a quest in memory (`poke` sQuestSelector +0xd68 = the quest, +0xd65 |= 1;
  `quickhunt.c`): the Bherna Chief, ahead of the spawn, gets the quest icon.
- Test runs can override the device's settings without touching its ini: `MHGU_LOWEND=0|1|2`, `MHGU_SCALE`, `MHGU_SHADOWS`, `MHGU_BLOOM`,
  `MHGU_GRASS`, `MHGU_FPS_LIMIT`, `MHGU_NO_REVERB=1`, `MHGU_UI_ANCHOR=Edges`. A phone on its charger reads charging current: unplug it to measure watts. At 30 FPS the GPU runs at
  lower clocks, so `gpuprof` times are longer: compare frame rates or watts, not milliseconds, across FPS limits.
- `[Performance] Grass` (#159): the grass fields are the game's uGrass2 units (`runtime/hle/grass.c`), one in each area that has
  one (22 areas of 10 maps; the Marshland is m07, its areas 1, 2 and 8). Off skips the unit's draw into each pass (vtable +0x30,
  0xb13490), live; its +0x24 only sorts its clusters, and skipping that changed nothing. The game draws no alpha to coverage there at
  1x or 4x MSAA, so the first Grass, which skipped those draws, did nothing. Galaxy S25 Ultra, area 8, standing: 83 fps on, about 100
  off. The weeds, reeds and lotus of an area's ground are part of the area's own model (uStage, with its terrain and trees) and stay.
  The tall grass an Adreno 640 on 1.1.128 doesn't draw (#171, #93: Jurassic Frontier's area 1, the Marshland's 8) isn't the
  port's Adreno path: on the PC, `MHGU_VK11=1 MHGU_VK_ADRENO=1 MHGU_VK_NO_BORDER=1 MHGU_VK_NO_BC=1 MHGU_VK_NO_INT64=1` draws
  Jurassic Frontier's area 1 grass as a plain run does (quest 101, `MHGU_QUICKHUNT_AREA=1`).
- Finding what draws something: the classes are the DTI names in the executable (each `MtDTI` constructor call, 0x9aedcc: r0 the
  DTI, r1 its name, r2 the parent, r3 the size; a class's vtable has its getDti at +0x14). In a running game `fnnew <lo> <hi> 1`
  lists the functions of a range that run, and `poke` of a vtable slot with the base class's empty one (cUnit's +0x30 is 0x21f858)
  hides a whole class at once, to see what goes.
- Internet hubs (`net/ldn.c`, the `server/` hub server): the Multiplayer page (Network before 0.1.9) puts the internet first: Hub
  server (a short list, `LDN_SERVERS`, Custom... and Off), Matchmaking key and Check hub server, which runs by itself as the page
  shows. Empty Server is `LDN_DEFAULT_SERVER`, `hunterhubid-gu-hub.duckdns.org`: a community server that speaks PROTO_VERSION 2 /
  LOBBY_PROTO 1 (the project runs none; never upstream's, whose owner asks forks not to use it). Keep PROTO_VERSION and the
  public room (no key: all zero) as they are, or 0.1.6-0.1.8 players stop meeting newer ones. Nothing reaches a hub server
  before the one-time notice about playing online was closed (`settings_panel_online_ok`, the thank-you's card;
  `[Multiplayer] OnlineNoticeShown`): the game's search waits under the card (up to 60 s: it runs on a thread of its own, and
  a search that finds nothing ends there) and a created hub waits to be listed. `MHGU_ONLINE_NOTICE=0|1` in a test run. MH4U for Android shows the same card and text.
- Crossplay with emulators (S36, 1.0; `[Multiplayer] Crossplay` = Off | Rooms | Ryujinx, the Multiplayer page's first
  section, `settings_panel.cpp crossplay_rows`): ldn.c's nn::ldn drives an `XBackend` (net.h) instead of the port's link:
  create, scan, connect, the game's datagrams (`ldn_send`), and the backend calls back `ldn_x_info` / `ldn_x_lost` /
  `ldn_x_packet`. Off, nothing of it runs (`X_be` NULL). While it is on (`ldn_crossplay_vanilla`) emsync, hpscale and
  Village quests in the Hub stay out (the others' games lack them; HP would differ). The game opens its UDP sockets with
  protocol 17 (`[net] the game's datagram sockets: protocol 17`) on port 12345; its local play version is 1.
  - `xroom.c`: a yuzu-family room member (ENet over UDP 24872, network_version 1; big-endian packets, strings and vectors
    with a u32 length). LDN over the room: scan (broadcast) / scan reply (NetworkInfo) / connect (NodeInfo) / sync / disconnect
    / destroy, raw SDK structs (Citron drops shorter ones). Addresses are the room's 192.168.x.y (host-order u32 in the
    structs), MAC 02:00:ip, mask /24 (x.255 is a broadcast). Datagrams: one zstd frame each (ours: raw blocks with the
    content size, which the emulators' decoder needs; theirs decoded with third_party/zstd). The protocol byte is the socket's
    `Network::Protocol`: 0 if the game gave none, else UDP = 3, but 4 in Eden 0.2.1+ (and the Suyu built on it): sent per
    member as learned (`peer_udp`), both when unknown; a port reads both, the second copy dropped (`seen_twice`). Room members'
    game info carries the game's version: another than 1.4.0 (or another local play version) is refused both ways
    (`wrong_version_notice`, and a line in the room's chat for the emulator's player). A hub's player missing from the
    room's member list counts as gone (the emulators don't say so).
  - `xroomhost.c`: RoomHost = On hosts such a room in the app (any address, RoomHostPort or 24872; `MHGU_ROOM_BIND=127.0.0.1`
    in tests, so Windows' firewall asks nothing) and joins it at 127.0.0.1. `xlobby.c`: Public rooms, `GET /lobby` of
    api.ynet-fun.xyz over plain HTTP, no account (the lobby Eden, Citron and Torzu use); rooms of netVersion 1 where 1.4.0 is
    played or preferred.
  - `ryuldn.c`: a Ryujinx LDN client (TCP 30456; 12-byte little-endian header 'RLDN', version 1; ConnectRequest is 0x500,
    the server's view of the network authoritative; addresses 10.114.x.y, mask /16; ProxyData with protocol 17; the
    server's pings echoed; a Ryujinx host's own proxy (P2P) followed when the server sends one). No server by default: the
    owner decided the player types one, and the help states the official server's rule (Ryujinx-to-Ryujinx only, IP bans
    for others) and that the choice is the player's.
  - Tested on the PC (hidden instances, the control file): two MHGU.exe met in a hub through E:\Yuzu\yuzu-room.exe and
    through a Python stand-in of the Ryujinx server (scratch, not shipped). yuzu-cmd never initializes its room network
    (its `-m` does nothing): a real emulator needs the Qt frontend, driven by hand.
- The port's chat (1.0, S32, after tabarong's PORT_CHAT.md): `runtime/chat/` is the same in MH4U's `app_native/chat/` (keep
  them identical); `runtime/chat_port.cpp` is MHGU's side, `runtime/settings_chat.inc` its menu pages (View::Chat, View::Friends,
  View::ChatOn), included by settings_panel.cpp. Hub: a signed "MHPC" line in `F_AUX` (`chat_aux_received` in ldn.c's two F_AUX
  paths; the host passes on only lines whose signature, bound to `ldn_party`'s id, holds). Global / General / friends: ephemeral
  Nostr events (kinds 25910-25912) on public relays, only after View::ChatOn; private ones NIP-44 v2; keys with libsecp256k1
  (`third_party/secp256k1`, `chat_secp256k1.c`); sockets `chat_ws.h` (Android: `ChatSocket.java` through JNI; Windows: WinHTTP).
  Everything of it in `chat.txt` beside the settings (the key, friends, mutes, blocks, its options, `flag` lines); nothing said is
  written. The button over the game and the lines beside it: `chat_overlay.cpp` (touch.cpp's `F_CHAT`, after the controls).
  S35: the key row's Generate a key (`chat_new_key`: 16 random bytes as 32 hex, the clipboard) and Share it in the chat. S34:
  `play_together_notices` (the Hub offline, once a session until a hub was entered; the first hub ever). A PC test of the hub
  chat: two MHGU.exe with their own MHGU_HOME / MHGU_SAVE_DIR, `Server = Off`, `Join = 127.0.0.1`, one Create (local), the other
  Search (local).
- The servers and keys typed and saved are remembered: `[Multiplayer] RecentServer1..` / `RecentKey1..` (`LDN_RECENT`, 24, the
  last first, none of the short list's; Config's `ldn_recent_*`, at its end), written only when there are some. Save adds a
  typed value when the row changes, never the one it replaces: one taken out with its × stays out, even while in use. A file
  of an older version (no `LockOnButtons`) gets the custom server and key in use put in the lists once, at the first start
  (`config_load`). They
  are listed in the row's picker between the short list and Custom... (the key's row is a picker too: Public, the keys,
  Custom...), each with an × that takes it out at once and in the file (`forget`: it is no setting, so Save and Discard don't
  touch it; the picker opens again as it is now, `reopen_picker`); a controller's top button or Delete does the same. MH4U for
  Android's Lobby row does the same with `[Network] RecentLobby1..`.
- The menu's keyboard view has Phone keyboard on Android (as MH4U's): `textinput.c` state 3, SDL text input with the screen
  keyboard (no capitals, no corrections); the IME's composing text arrives as typed text and Backspaces, and Enter accepts.
  Back only hides the keyboard (SDL stops text input by itself: `textinput_menu_keyboard_closed`, back to state 2): the view
  and its text stay, its key brings the keyboard back, and a second Back leaves the view, as in MH4U.
  The menu's own keys stay for a controller; a physical keyboard types in both.
- While a menu of the game's is open the touch layout doesn't change: the menu pad shows besides (`C_MPAD`, `[Touch] ShowMenuPad`,
  saved as `menu_pad` in touch_layout.txt) and presses the d-pad, its page tabs above it (`C_ML` / `C_MR`: the pair the game's option
  turns pages with, `menu_pages_zl`), the other pair above them with `ShowMenuZL` / `ShowMenuZR` (off by default) and a small Minus
  below it while the hunt's is hidden (the item box opens its other sections with it). The game reads what the pad alone presses
  only while a menu takes input: a menu's cursor (`cCursor`, `cBoxCursor`, a quantity, a yes / no: hle/mousemenu.c, hooked on both
  platforms) ran in the game's last update, or the hunter isn't played (`menu_takes_input`, gated in hle/input.c as the game reads
  the controller). A hunt's menu closing runs no cursor while its windows still fade out, and the hunter takes commands again from
  the next update on (an X there drew the weapon): a d-pad press then is a Hunter Art. The stick keeps walking (the pause menu, the
  pouch and the town's item box don't read the stick); it moves the cursor where the hunter isn't played (the title, the main menu)
  and in a town's menu once the player hasn't moved for 300 ms of tilt (the item box, a shop). Test with `finger` and `touchedit`
  (MHGU_CONTROL); `menutrace` shows each pad update's buttons and how many updates ago a cursor ran. In a town's menu (no Hunter
  Art there) the pad's presses pass while its windows show, a cursor run or not (sorting in the item box, the pad pressed nothing
  there: #174). A window that came without a press (`game_menu`: a bubble) becomes a menu once a cursor of the game's runs: the full
  pouch's comes as a long gathering ends, past `WINDOW_PRESS`; `fakewindow <ms>` in the control file shows such a window. A model
  drawn in a town's menu (a shop's or the smithy's weapon, the item box's hunter: a depth-tested draw in a viewport smaller than the picture,
  `touch_hud_preview`) turns with the sticks: the touch stick and drags are the game's sticks there, never its cursor.
  So does the hunter in the item box (shown up close, its "Turn" hint): a stick that turns him where he stands stays the
  game's (`[touch] the stick turns the hunter in this menu`); up to 0.1.9.3 it became the cursor 300 ms in, and he stopped
  (#162, #186). A talk's menu (the house Felyne's, RG406V) turns nobody: its stick moves its cursor.
- The menu pad's presses are no longer dropped (up to 0.1.9.2 they were): while no menu's cursor ran in the last update (`menu_takes_input` false)
  hle/input.c doesn't give them to the game as the d-pad (no Hunter Art) and hle/mousemenu.c `pad_update` writes them into the
  menu's half of the game's pad (sFestaPad +0x6bf4 held, +0x6bfc pressed, +0x6c34 repeated), which the hunter never reads, as the
  desktop's mouse does. So menus that run none of the counted cursors take them: the Options' values, a talk's yes / no (#196,
  #210, #215, Discord). A dialog is a menu for the touch controls too (`menu_dialog_age`: the talk window's
  update 0x83d010 ran, or the message window 0x84bc40 waits for a button, as the desktop's mouse sees them): a talk's window sits
  where the chat's log does, which is no menu window here, so the Transpurrter's "cancel the delivery?" had no menu pad (#210).
  A town's world map (X) and the "Choose a destination" list after it (the village, the hunter's house) are neither: no window
  the HUD's layers tell, no talk, only a cursor of the game's running, and the hunter counts as played (the town's update runs
  under them). Outside a quest such a cursor makes a menu (`town_cursor_menu`; in a town none runs but a menu's), so the menu
  pad shows there and the stick moves the cursor (#163, #216; up to 0.1.9.3 only the highlighted place could be taken).
  `[touch] a model in the menu` and `[touch] the player walks
  nowhere in this menu` are logged always (a model that stops turning after a moment, Low-end at 50 %: #162, #186, Discord).
- A text entry of the game's (the hunter's name at character creation...) shows a Keyboard plate at the top while the port's
  menu is closed (`g_text_plate`): a tap opens the menu's keyboard view for it (`SETTINGS_VIEW_GAME_TEXT`), Accept types the
  text into the entry and confirms it, leaving the view closes the menu. The keyboard view opens with the phone's own keyboard
  up on a phone played by touch, in both ports (its key had to be found first: "the phone's keyboard was taken out", Discord).
- Android lists every input device in the log (`[input] device ...`: sources, a gamepad's keys, read as a controller or not;
  `[input] keys from device ...`: a gamepad's keys from an external device that isn't read as one, which SDL gave the game as a
  keyboard's), at the start and when a device comes or goes or such a key first comes (`GameActivity.inputReport`,
  `SDLControllerManager.keySeen`): an Ipega PG-9025 Android calls "... Keyboard" left no line at all (#87, #29). Letters aren't
  logged (a keyboard's typing in a shared log).
- 1.0's input group (branch `feat-input`): no double tap on the camera's part does anything (it pressed L, the camera's
  centring: by accident in a fight it turned the camera away, #130, #132; the L button does it). Three `[Touch]` switches, the
  touch_show array's last (`TB_MENU_LR`, `TB_DPAD_MENUS`, `TB_ARTS_PAD`): `ShowMenuLR` (On: the menu pad's L and R tabs),
  `DpadInMenus` (Off: On keeps the hunt's d-pad on the screen in the game's menus, taking fingers, `under_mpad` / `hit`;
  #196), `ArtsWithController` (On: a tap on a Hunter Art's panel while a controller hides the controls, 0.1.9's #48; Off: no
  taps there). MH4U for Android has the first two (`show_menu_lr`, `show_dpad_menus`). A keyboard and a mouse on a phone
  (`GameActivity.keyboardMouse`: an external keyboard with letters and an external mouse; `host_input_keyboard_mouse`, asked
  every 2 s and as one comes or goes, logged `[input] keyboard ..., mouse ...`): with both, Controls shows a Keyboard and
  mouse header with the PC's Keybinds view and Mouse sensitivity, a combined button's view its Key row, and the key capture
  takes a mouse button (never a finger's tap: `overlay.cpp finger_mouse`). Nothing else changes: the keys and the click
  that captures the mouse worked on a phone before, with a phone's default keys (`DEFAULT_*_KEYS`, `#ifdef __ANDROID__`).
  The lock-on by a tap on the target camera's icon is gone since 0.1.9.3's Lock-on button (the icons take no taps; config.c
  turns L and ZL on once for an older file): nothing of it is left.
  A captured mouse on Android sends ACTION_MOVE and ACTION_BUTTON_PRESS / RELEASE from SOURCE_MOUSE_RELATIVE: SDLSurface's
  onCapturedPointerEvent now gives them to SDL as relative mouse events (it passed them to onGenericMotion, which takes a
  free pointer's HOVER_MOVE only, so a phone's mouse never turned the camera), and keyboardMouse counts a captured mouse
  (SOURCE_MOUSE_RELATIVE) as one. Seen on the RG406V (2026-11-13) with a keyboard and a mouse made by the shell's `hid` tool
  (`hid <file>`: JSON lines `register` with a boot keyboard's or a 3-button mouse's descriptor and `"bus": "bluetooth"`,
  `report`, `delay`; external devices for Android): the log's device lines and "[input] keyboard connected, mouse
  connected", Keybinds and Mouse sensitivity under Keyboard and mouse, a key chosen in Keybinds (F), a click in the game
  "[input] capturing the mouse", 60 reports of 30 counts turning the hunt camera in quest 101 (nothing before the fix),
  Escape "[input] mouse released". A click at the screen's very corner reaches no app (the system's edge). Also seen
  there: no double tap centres the camera; with DpadInMenus the hunt's d-pad stays in the pause menu and moves its cursor,
  and the menu pad shows without L and R with ShowMenuLR Off; with the lock-on a bow's draw from the stance turned the
  hunter to the monster ("[lock] a bow's locked shot: heading bf1c -> 02c1 (action 0010:002)", training quest 30256 with
  MHGU_QUICKHUNT_AREA=1). The tutorial's and the pause menu's pages ("< 1 / 3 >") turn with left and right, not with R:
  the menu pad's R tab and a keyboard's R did nothing there, the pad's right turned them (the game's own).
- The Hunter Arts' pieces on layer 100 are taken from y 560 to the canvas's end (720): a style with fewer than three arts draws
  the empty slots' frames below it (one art: rows at 676, 721, 766), out of sight on the console, and the touch arrangement lifted
  them by the top row into blank rows (#125). With Control Type 3 the arts' "ZL +" are on layer 100 at x 688 and 692, left of
  the art's button (726) on its row (hudtrace on the RG406V, Adept with one art): pieces left of x 700 go with the arts only on
  a row an art was drawn on (`arts_row_has`); up to 0.1.9.3 nothing took them and they stayed by the item bar. Layer 103 left
  of x 950 is taken for the arts too (the item bar's pieces start at x 1005 even with L held: hudtrace on a Moto g06).
- Phone tests of the touch controls without the player's save: `MHGU_SAVE_DIR` to a copy and `MHGU_CONFIG` to a copy of the ini
  (`Controls = On`). On the Moto g06 adb can't read the app's save files (the app's uid, rw-rw----): copy a save from another phone's backup.
  A folder the game made in an `MHGU_SAVE_DIR` (a new game's save_0) is the app's and adb can't remove it.
- The HUD's parts (`runtime/hud_parts.h`: the item bar, the Hunter Arts, the target camera, the clock, health and stamina, the
  sharpness and gauges, the party, the minimap) are found by layer and place in `cbuf_info` (`nvn/vulkan/exec.c`; the status, layer
  103, split by region, a piece in none going with the nearest part: `hudtrace` says which part and "nearest") and placed by
  `hud_place_*`, which the touch editor uses for its estimates too. Two arrangements, saved as `hud_<key>` and `pc_hud_<key>` in
  touch_layout.txt (offset, size, opacity, hidden; files with only offset and size read the same): `HUD_TOUCH` while the touch
  controls show with MoveHUD On, `HUD_PLAIN` otherwise (a controller, a keyboard, MoveHUD Off); the touch editor edits either
  (its HUD switch), a desktop's Move the HUD all of the plain one's (the same frames and names: draw_part_frames, part_frame). Left at their defaults the parts land exactly where
  they always did: keep it so. A menu of the game's is told by `touch_game_menu`, not by a layer 97 window (the chat's log, a
  bubble, an aim, the Quest Complete stamp are drawn there too): with one open a part takes only pieces at places it had in the
  last 1.5 s, and a moved part on its window isn't drawn. The hunt's pause menu and pouch draw only the status, before their
  windows; the minimap has no scissor.
- The Options' third page is in sUserInfo (`*0x1a97f78`), a byte each, written as soon as an option changes there: +0x446c Control
  Type (0..3: Type 1..4), +0x446d + Button Controls, +0x446e L/R Button Controls (`menu_pages_zl`), +0x446f R Stick Controls; the
  input shown beside each Hunter Art follows them at once (poked in a hunt: Type 2 B, Type 3 X, Type 4 and the swapped stick the
  right stick). The earlier pages are in sGameControl (`*0x1a8ca48`) from +0x5e in the menu's order (the music +0x5e, the camera
  angle +0x63, Bowgun Controls +0x67, Bow Controls +0x6c). A tap on a Hunter Art presses what the Control Type uses for it
  (`art_press`, touch.cpp): Type 1 the d-pad up / right / down, Type 2 R + A + B / ZL / ZR, Type 3 ZL with X / A / B, Type 4 the
  right stick (`touch_right_stick`), each pressed at once for 120 ms. R + A + B at once is taken (tested at a high frame rate:
  R and A held then B, or all three together). With an empty gauge ZL + X gives X's own move (the Great Sword's draw slash), at
  once or with ZL 5 frames ahead; an art with a full gauge was not seen from a tap yet. `MHGU_LOG_MOVES=1` logs what each started.
- A Prowler is `uPlayerQuest15`: vtable 0x19515ec, the last of the sixteen with `FN_DRAWN` in slot 94 (the base class first, then
  00 at 0x1949a88, the Great Sword's, seen on a Motorola g06), so `weapon_hunter` finds it. Its item bar holds Support Moves, chosen
  with L + Y / A as the game does (`support_step`), never through the hunter's pouch slot. Not yet seen on a device: a Prowler is
  chosen at the item box, and `MHGU_QUICKHUNT` with a Prowler quest (100201) departs the save's hunter as it is.
- Android/data on Android 11+: a file or folder another app made there (a file manager through Shizuku, adb) keeps that app as its
  owner, and the app can't write it (gu-native #176: game_folder.txt "open failed: EACCES"). `AppHome.ensure` runs at every start:
  in the folders the app writes (`MHGURecomp`, `save_import`, `cheats`, `cache`, `shots`) and among the files at the top, a foreign
  file is copied and put in its place and a foreign folder is renamed aside (`<name>.other-app-<date>`) and made again (any other
  folder, an agent's backup made with adb, is never touched); if `files/` itself can't be written (EACCES/EPERM only), the app moves for good to
  its private folder (`home.txt` in `getFilesDir()`, which `runtime/main.c` reads for `MHGU_HOME`) and says so once; game files
  put in the old folder's `data/` stay there, named by the private folder's `game_folder.txt` (`keepGameFiles`). Test with
  `adb shell` (the shell uid, as Shizuku's): `echo x > .../files/game_folder.txt` after deleting the app's own.
  A replacement: the copy as `<name>.mine.tmp`, the original aside as `<name>.other-app.tmp`, the copy into the name (copied under
  it if neither can be renamed back); one cut short is put back at the next start, the app's own copy first, and a lone `.mine.tmp`
  never takes the name. Copies to the private folder go through `<name>.copy.tmp`. Seen on the RG406V (2026-11-10, MH4U the same).
  `repair` stops after 4000 entries in all: there `cache/` had 5361 and the top-level files listed after it were never looked at,
  so test a replacement inside `MHGURecomp/`.
- The save folder (`runtime/savefolder.h` lists its files): only `save_0` moves (`services/fs.c` asks `savefolder_root` at each
  mount; the game mounts at every save), `g_save_dir`, `bonus/` and `save_backups/` stay local. A folder that can't be reached at
  a save mounts the local one instead and writes `save_folder_missed.txt`; the start gate (`SaveFolderActivity.gate`) asks.
  Missed once, the local one is used until the game closes (`g_own`), with a notice: the game holds the save it read, and one
  read locally because the folder wasn't there yet went over the folder's newer one at the next save once it came back.
  A `.mhgu-commit` of `system` / `system_backup` is applied only at the save's full size (a sync app can bring one half-written).
  The in-use mark `.mhgu-in-use` is renewed every 5 minutes (`savefolder_tick`) and counts for 15.
  Its browser offers an SD card only when `AppHome.probe` can write at the card's root (`FolderBrowser.roots(c, true)`), and the
  intro names the card only from Android 11 (Android 9 and 10 let no app write on a removable card outside its own folder); the game
  folder's browser lists every card. RG406V (Android 13): the SD card tab shows and lists the card, the probe leaves nothing there.
- Quit, Save and restart and the window closed park the threads that draw before `exit()` (`ui.c leave`, `device.c vk_exit_begin`):
  `exit()` destroys the C++ statics made after `backend_init` (`overlay.cpp`'s turned copy of the menu) before it reaches
  `exit_handler`, and the update thread, drawing the held menu with them, crashed in `free()` as Save and restart left.
- The settings menu holds the game when the player is alone (`[Application] PauseInMenu`, Android: `overlay.cpp` `menu_holds_game`,
  never with an LDN network up): the update thread stays in `overlay_frame`, drawing the menu (`frame_once`) and presenting the
  last picture again (`vk_present_again`, once the game's own presents stopped 50 ms ago), with the guest clock held and the
  presenting, vsync and audio threads waiting (`vi_menu_hold`, `vi_pause_wait`), as in the background. MH4U for Android does the
  same with its clock (`android_main.cpp`, `[Interface] PauseInMenu`).
- The two ports look and read alike (0.1.9's parity pass, 1.0's menu): the menu's sections in the same order and names,
  a phone's 3D resolution by Low-end mode and the frame rate (`scale_row`; a desktop's while its Render resolution is
  Auto), the Touch controls / Controller pages and Your game / The app, Menu colours on phones too (listed Hunter, Gold,
  Blue; the ini keeps `THEME_*`), one FPS preset list (30, 60, 90, 120, 144, 240), the titles in the phone's Noto Serif
  (`overlay.cpp`'s second font, `title_font`; a desktop has its one font), Credits & Licenses as lines between the rows
  (`note`), a chevron on a row that goes on, the hunt buttons at half while a menu of the game's is open and those under
  the menu pad's tabs faded out with it (`under_mpad`), the hunt's d-pad (ShowDpad) too wherever it is, the menu pad
  taking its place (`hit` skips it meanwhile; 4u-native #25), the item bar's arrows on by default, one each side of the
  bar, as large as the room beside it allows (`item_arrow`; MH4U's `place_raw` has the same rule), and the same Spanish
  words (palanca, Envainar, Evasión, Guardia / correr, Diseño, barra de objetos). A name, a text or a default changed in
  one port is changed in the other (MH4U for Android: `app_native/settings_panel.cpp`, `settings_locale.h`, `touch.cpp`).
- The menu of 1.0 (`E:/claude_trabajo/v1.0/MENU.md` has every row's place, its English and Spanish): six sections in the
  column at the left, each with its mark (`section_icon`: Controls, Graphics & Audio, Gameplay, Multiplayer, Mods &
  Cheats, System), and Credits & Licenses as a smaller plate under them (MH4U's GPL additional terms name that page).
  A section's page holds its basic rows; a row with an arrow opens a page one level down (`View::Page`, `State::at`:
  Touch controls, Controller, Keyboard and mouse, Picture and sound, Screen and HUD (the Display tab of 0.1.9),
  Advanced, Monster health, Local network, In the hub, Mods, Cheats, This menu, Your game...). Every option is an entry:
  `MENU_ENTRY(page, order, rows, reset, shown)` adds its rows to a page at an order (the old rows use multiples of 10),
  puts its options back for Reset ("Reset this section": every page of it; "Reset this page": that one) and can hide
  itself; `MENU_PAGE(id, title, parent, order, shown, link)` makes a page, whose row shows only once something on it
  does (the places kept for 1.0's options are pages with nothing on them yet). No paragraphs on a page: a row's help is
  its "?" at the right (`help_seal`), opened under the row (`help_box`) by a tap, Y (the left face button) or H; a
  desktop's mouse-and-keyboard layout keeps the help of the row pointed at over its guides. A strip at the foot says the
  last save's outcome and the restart mark and, with a controller, its buttons (`guide_button`, in the touch controls'
  style). No option's key, value or default changed with it, only its place.
- 1.0's layout group (`runtime/touch.cpp`; the MH4U port's `app_native/touch.cpp` the same, names, files and texts):
  - Control profiles (S12, after tabarong's design, credited in the README and Credits & Licenses): 10 slots, each a whole
    layout of either kind, as `profile_01.txt`..`profile_10.txt` (touch_layout.txt's lines) in `layout profiles/` of the save
    folder chosen (`savefolder_chosen`), else of the working folder; the pictures in its `images/`. touch_layout.txt stays the
    layout in use with a `profile <n>` line; the editor's Save writes both (`profile_store`). A touch_layout.txt without that
    line (made before 1.0) is copied byte for byte into profile 1 (another one empty if profile 1 holds a different layout:
    a save folder two devices share) and the line is appended (`profiles_migrate`); no touch_layout.txt and a profile 1 in the
    folder: that one is played (a reinstall). A save folder chosen later gets the app folder's profiles copied once
    (`profiles_adopt`). The editor's Profile button opens its window (Load, Save here, New of either kind, Delete; a second tap
    confirms a replace or an empty), the settings' Controls a page (`View::Profiles`, `touch_profile_*`: Use, Save the
    current layout here, New, Edit, Clear, at once). The desktop's HUD editor has the same window.
  - Emulator-style layout (S09, `Layout.kind` = `KIND_EMU`, `layout_kind 1`): `C_EA`..`C_ER3`, each the game's button alone
    (`DEFS[].buttons`, no special case), in the port's own button look; the stick, settings, Lock-on, item arrows and
    combined buttons shared (their emu places: `EMU_PLACES`). No menu pad, no ranged layout (`ranged_layout` false). Each
    control but the settings button can be hidden in its editor (`Layout.off`, `hidden <key>`; `hideable`): the port's own
    layout keeps the settings' Show* (no per-control Hide there: S23).
  - Held buttons (S13, `Layout.hold`, `hold <key>`): the editor's Hold switch on a picked button (`holdable`: not the pill,
    band, stick, d-pads, menu pad, arrows, settings, Lock-on or aim); in play a tap latches what it presses (`State.latched`,
    `latch_btn`, ORed in `recompute_held`), lit with a closed padlock, the next tap lets go; all let go with the controls
    hidden, the editor, the background, or the control leaving the screen (Run in a menu, the ranged layout's).
  - Pictures (S06): the port keeps its one button style; `image <key> <file>` puts a player's picture on a round button
    (`image_ok`), its letter's badge kept. Read with SDL's PNG/BMP loader, at most 256 px, as ImGui user textures
    (`pic_texture`: `RegisterUserTexture`, `GetTexRef`, uploaded by the overlay with the font). Added by the editor's Add
    pictures or the settings' Button images: Android's `PicturesActivity` (any picture Android decodes, made PNG at 256 px),
    a desktop's file dialog (`pc_import_pictures`, PNG and BMP).
  - The FPS counter (S24): picked and dragged in both editors (`SEL_FPS`, `fps_counter x y` in the layout: its middle in
    screen fractions), drawn there by `perfstats_draw` (`touch_fps_place`); `perfstats_bottom` is 0 when it isn't at the top.
  - Floating stick (S07, `[Touch] FloatingStick`, `TB_FLOAT_STICK`, off): a thumb within 2.6 stick radii (0.3 screen heights
    at least) of the stick's place brings its base there (`stick_reach`, `lock_geo` for the sprint lock); never the whole
    left half (the owner's call).
- Bugs that only show on some devices are common here. Test on a real phone, and on a device without Snapdragon.
- `[Performance] Effects` (1.0, `runtime/hle/effects.c`, live, On by default; the MH4U port has the same row): the game's
  effects are units of a few classes (the DTI names: each `MtDTI` constructor call, 0x9aedcc, gives the DTI object; a
  class's getDti returns it and sits at +0x14 of its vtable, so the vtable is found by that function; slot +0x30 draws the
  unit into a pass, as uGrass2's): uEffect (vtable 0x198d620, draw 0xbba5b0; uProofEffect and uMHProofEffect inherit it,
  uProjectionEffect's 0xbc0b84 calls it), uSimpleEffect (0xe667a0), uCameraEffect (0xbb827c). Off, those draws aren't run
  (fnhook, as Grass): the effects still play, nothing of their particles is drawn. uEffect2D (0xe5c80c, the HUD's 2D
  effects) and uProofEffectModel (0x52b8c0) stay (`MHGU_EFFECTS_HOOKS=<bits>` tries them). `MHGU_EFFECTS_LOG=1` logs each
  class's first draw and every 10 s how many times each drew (PC, quest 301's area 6: uEffect about 2000 draws in 10 s, none
  of the other two); `MHGU_EFFECTS_FLIP=<s>` turns them on and off in turns (a test). The volcano's smoke in Jurassic
  Frontier's sky and the cooking pot's steam in Bherna stay with Effects off: part of the stage's models, not effect units.
  `MHGU_EFFECTS=0|1` overrides the ini in a test run. The scripts that found the classes and vtables (from a decompressed
  `main` and `generated/main`): E:\claude_trabajo\v1.0\out\gfx\tools (dtis.py, props.py, strrefs.py).
- 3D resolution (1.0): `[Display] RenderScale` takes any percent from `RENDER_SCALE_MIN` (10) to 100, the 3D picture never
  under 320x180 (`config_load`). The menu's row (`scale_row`) is a number: left / right one percent, a tap the usual ones
  (100 ... 20); on a phone the list ends with Exact size, which sets `[Display] RenderResolution` to the percent's size and
  shows Width and Height rows (a desktop has them as Render resolution Custom). A phone's exact size is kept within 320x180
  and the screen, and with Low-end mode gets its HUD stand-ins as a percent does (`lowend_hud`). The phone's help says why
  the HUD softens with it: it is drawn at the 3D size unless Low-end mode or FSR draws it at the screen's (1.0's S22: kept so,
  a full-size HUD costs a weak GPU more than it gains).
- Clear shader cache (1.0, `runtime/cache_clear.c`, both platforms, Graphics & Audio's last row): a tap writes
  `cache/clear_at_start` (another tap removes it); the next start deletes `cache/programs.bin`, `programs.src`,
  `pipelines.bin`, `vk_pipelines.bin`, `spirv/` and `arcmerge/` before anything reads them (`main.c`) and logs "[cache] the
  shader cache cleared ...". PC: 2947 files, 367.5 MB. The overlay's plate "Building shaders" (`overlay.cpp shader_plate`,
  `exec.c vk_shader_progress`: the start's queued builds and the prewarm's translations, done of total) shows once they
  have taken 3 s and while they last, then "Shaders built" for a moment; the log says when it showed and when it ended.
  PC after a clear: "353 of 6627 built 3.0 s after the overlay's first frame".
- What the PC showed of the 1.0 graphics work (E:\claude_trabajo\v1.0\out\gfx: `pc_run.sh`, `evidence\`): the menu's rows
  and their EN/ES help (Effects, the 3D resolution at 95 % with its size and the presets list, Clear shader cache with its
  size), the clear at the next start and the plate ("965 de 7021 (13 %)" over the first start's credits). Effects Off: the log
  says each class's draws left out, and uEffect's draw is what reaches its particle managers (it walks the unit's list at
  +0x1f0 and calls each one's +0x54); no scene tried on the PC (Jurassic Frontier's camp, Bherna, the Deserted Island's area 6
  with Jaggi near, a great sword's charge) showed a particle big enough to see go, so the picture with and without is still
  to be seen in a hunt with hits on a phone (the MH4U port's same switch removes the sand ship's dust and the sun's flare in
  its PC runner's deterministic G2). Owed on a phone (the 2026-11-13 phones had a Play Protect dialog in front; nothing was
  installed): E:\claude_trabajo\v1.0\out\gfx\phone_tests.sh <serial> gu (Effects in quest 301's area 6, the menu's rows on
  a phone, 57 %, the clear and the plate, and MHGU_PERF_MENU_PRESENT 0 / 1 for 1.0's S25), and Exact size by hand.
- Texture packs are romfs mods (they replace the game's `.tex` files): `[romfs] mod <name>: N files (X MB), T of them
  textures (Y MB as files ...)` says what each brings. No pack format of the emulators' (KTX2 and the like) applies here: the
  game reads its own `.tex` files, and a pack's BC textures are decoded on the CPU on a Mali as the game's own are.
