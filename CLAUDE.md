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

To diagnose a device: `adb logcat -s MHGU`, or `Android/data/com.mhgu.recomp/files/log.txt`. A player whose game stays black can
still share the log: a long press on the app's icon offers Share log (`res/xml/shortcuts.xml`). The log's `[vk] limits` line says
what the GPU allows (vertex streams, a stage's buffers and textures...).

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

## Testing on a device

A test build (`MHGU_DEV=1`) reads two files in `Android/data/com.mhgu.recomp/files`; a public build reads neither:

- `env.txt`: `NAME=value` lines. `MHGU_QUICKHUNT=101` goes straight into a quest, `MHGU_VK_STATS=1` logs statistics,
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
- Pacing and threads, switches being measured (0.1.9's performance pass; each read once at the start, off unless set to 1, which is the
  old behaviour, until measured): `MHGU_PERF_ADPF70` the ADPF target 70 % of the frame period at FPS limits of 60 and below, as in MH4U
  (at all of it the system lowers the clocks until the busiest thread fills the period: the Moto at Toaster 30 worked 25.5 ms a frame on
  average, 33 at most, of 33.3), the efficient cores then judged against the period. `MHGU_PERF_EFFICIENT` a frame over the target on
  its own neither turns the efficient cores off nor keeps them away (`efficiency_follow`; two in a window, or another within 5 windows,
  still do); `MHGU_ADPF_EFFICIENT=force` holds them on, to measure what they can save. `MHGU_PERF_LIMITER` the engine's frame limiter
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
  thumb pushes up past the rim; slid into it (within 0.55 radii of its middle) the run locks with a click and goes on forward
  (the stick held full up: the camera steers) while the thumb stays past the rim and after it lifts, until the stick is touched
  again, the thumb comes back inside the rim, the weapon comes out, a menu opens or the hunter isn't played (an area change
  too: `run_end` logs why under MHGU_TOUCH_LOG). It locks for a thumb that landed anew (not in the circle) or came back
  inside the rim since a lock ended (`run_armed`). X holds the run's R back for 400 ms: a thumb at the rim through a sheathe started R in the same update
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
- A menu can't be told from a window over the hunt: no flag in the hunter or the game's globals changes, and the hunt camera runs under menus too. `game_menu`
  treats a window as a menu when the hunter isn't being played or a button that opens one was pressed just before. Without a window,
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
  format, depth format, samples, draws), the programs drawn (vertex / fragment code address), drawn clears and copies, then the GPU
  driver's own lines from the app's logcat (`lost_report`, `exec.c`). The submit that reports it is often not the one that faulted.
  `devicelost` in the control file fakes one. Each kind of depth copy or blit is named once in the log where the game first does it
  (`[vk] depth blit ...`, `depth_copy_note`): on the Adreno 618 of #160 (1.1.87, Verdant Hills area 5) they are a suspect, not a proven cause.
- A romfs under 12.3 GB with all 35,531 files is an incomplete copy: a black picture, then `no function at 0`, or a game that closes in one
  area. None of the game's files is empty. Under 12.0 GB or with empty files (mods' aside) the log warns and, a few seconds in, a notice
  tells the player (`romfs_incomplete`, `overlay.cpp romfs_notice`; `MHGU_ROMFS_SHORT=11.5` shows it on a complete copy).
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
  the builds queued at the start are done` line says how long the start's builds took (Moto g06, warm caches: 1849 in 2.6 s).
- Phones give memory back (`staging_trim`, `vc_trim`, `exec.c`): a staging chunk the ring hasn't taken for 8 s, and vertex cache ranges no
  draw used for 20 s. Before, both only grew: the chunks a burst of uploads made (an area's textures, decoded on a Mali) and the
  vertex data of every area visited, up to the cache's 384 MB, which a 2.8 GB phone couldn't hold (#59, #156). The vertex cache is
  192 MB at 4 GB of memory or less, 128 at 3 GB. Under 350 MB of the system's memory available (`memory_short`: Android 14 and newer
  no longer tell a foreground app memory runs low) both wait 2 s instead. `MHGU_LOWMEM_MB=<n>` sets that threshold;
  `MHGU_VK_STAGING_TRIM=0`, `MHGU_VK_VC_TRIM=0`, `MHGU_VK_VC_MB=<n>`: as before;
  `MHGU_RAM_MB=<n>` takes the device's memory as that much (the texture and cache choices of a smaller phone).
  Measured on the Moto g06 (3.7 GB, Mali-G52), `MHGU_QUICKHUNT=101`, three minutes into the quest: VMA's memory 540 MB with 0.1.8's
  behaviour (the three variables above; a 365 MB vertex cache), 380 MB now, 348 MB with `MHGU_RAM_MB=2800` and the memory short;
  `dumpsys meminfo` GL mtrack 631, 500 and 453 MB, total PSS 1377, 1273 and 1241 MB; the same frame rate. A staging chunk is a VMA
  allocation of its own and goes at once, but a vertex cache range shares VMA's 32 MB blocks with images: memory goes back only
  when a whole block empties (56 MB of ranges dropped gave back one block). A pool of its own for the cache would give more back.
- A player's driver is checked once the device is made (`gpu_driver_check`): no library from its folder loaded means the system's runs,
  and the choice moves to `failed`. `DriverActivity` renames a library named as the system's (`vulkan.adreno.so`) to `vulkan.player.so`.
- The Khronos validation layer: put `libVkLayer_khronos_validation.so` (Android binaries of Vulkan-ValidationLayers) in `jniLibs` for a
  test build only, and `MHGU_VK_VALIDATION=1`. Never in a public APK.
- `minSdk` is 28 (Android 9). `./gradlew lintRelease` lists calls newer than that (`NewApi`); `native/build_native.sh` builds for API 28.
- Built for API 28, every `__thread` variable is emulated TLS: a call to `__emutls_get_address` and a `pthread_getspecific` per access.
  `rt_dispatch`'s cache was one, on every indirect jump of the game (a tenth of a game thread on a Moto g06): it is now per `Cpu`, through a
  pointer in `Cpu._pad` (`dispatch.c`, bytes the game's code never touches; `cpu.h` itself must not change). Keep `__thread` out of hot paths.
- Profiling: a test APK with `<profileable android:shell="true"/>` (put it in, build, take it out), NDK `simpleperf record --app` on the
  device, `binary_cache_builder.py -lib native/build/arm64-v8a`. `--call-graph fp` stops at the Mali driver; `dwarf` gets through. The
  stripped Mali library shows its own code as `stdlibp_neon_f32x16_to_f16x16`: that is the driver recording draws, not a conversion.
- The audio renderer had 70 effect slots of 1.7 KB, searched for every order number on every mix: over half the audio thread. `fx_order`
  lists them once after an update (`services/audio.c`). `getenv` walks the whole environment: never call it per frame, cache it.
- `gpuprof` shows the window image's pass (the game's picture copied in, the overlay) as its last line. The game's own passes all load
  their attachments; one whose first commands clear an attachment whole begins with `LOAD_OP_CLEAR` and those values instead
  (`noload_clears`, `render_pass`), and the clears it already did are skipped (`pass_skip_clear`). Not `DONT_CARE` plus the clears: an
  Adreno 830 then drew the scene black but for some tiles. Anbernic (Mali-G57) GPU frame -6 %, Adreno 830 -0.7 %. `MHGU_VK_NOLOAD=0`: off.
- Descriptor sets: `stage_set` reuses one written earlier in the same submission when every write is the same (about a quarter of the
  sets), and draw skips binding the same two again (`SS_SETS`; any other graphics bind forgets them). Zeros, the identity rescale table,
  the alpha reference and each stage's driver constants are copied once per submission (`shared_*`, `zero_ubo`), or every set would be
  new; a storage buffer of zeros is shared only by shaders without `uses_gmem_store`. `MHGU_VK_SETREUSE=0`, `MHGU_VK_SHARED=0`: off.
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
- A Galaxy (Samsung) refuses every simpleperf event for apps (`cpu-clock`, `task-clock`, `cpu-cycles`): profile the CPU on another
  phone. Its frame is CPU-bound: the GPU span in `gpuprof` includes the gaps where the GPU waits for the render thread.
- `[Performance] LowEnd` is Off, On or Toaster (2). On a phone (a desktop's: Building for Windows) everything that tests `low_end`
  treats Toaster as On; Toaster adds a HUD at most 540 lines tall and smaller BC textures, and the menu sets 25 %, 30 FPS, Shadows,
  Bloom, Reverb and Grass off (leaving Toaster gives the Reverb and the Grass back, as leaving Low-end does the rest).
  `[Performance] Bloom` (the bloom and depth of field chain, `lowend_skipped`; not with real FSR: the plain scaling sets `g_fsr_on`
  too, so test `g_config.fsr`) and `[Audio] Reverb` are switches for everyone. A file with LowEnd on and no Bloom key gets Bloom off,
  as before.
- Test runs can override the device's settings without touching its ini: `MHGU_LOWEND=0|1|2`, `MHGU_SCALE`, `MHGU_SHADOWS`, `MHGU_BLOOM`,
  `MHGU_GRASS`, `MHGU_FPS_LIMIT`, `MHGU_NO_REVERB=1`, `MHGU_UI_ANCHOR=Edges`. A phone on its charger reads charging current: unplug it to measure watts. At 30 FPS the GPU runs at
  lower clocks, so `gpuprof` times are longer: compare frame rates or watts, not milliseconds, across FPS limits.
- `[Performance] Grass` (#159): the grass fields are the game's uGrass2 units (`runtime/hle/grass.c`), one in each area that has
  one (22 areas of 10 maps; the Marshland is m07, its areas 1, 2 and 8). Off skips the unit's draw into each pass (vtable +0x30,
  0xb13490), live; its +0x24 only sorts its clusters, and skipping that changed nothing. The game draws no alpha to coverage there at
  1x or 4x MSAA, so the first Grass, which skipped those draws, did nothing. Galaxy S25 Ultra, area 8, standing: 83 fps on, about 100
  off. The weeds, reeds and lotus of an area's ground are part of the area's own model (uStage, with its terrain and trees) and stay.
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
- The save folder (`runtime/savefolder.h` lists its files): only `save_0` moves (`services/fs.c` asks `savefolder_root` at each
  mount; the game mounts at every save), `g_save_dir`, `bonus/` and `save_backups/` stay local. A folder that can't be reached at
  a save mounts the local one instead and writes `save_folder_missed.txt`; the start gate (`SaveFolderActivity.gate`) asks.
  Missed once, the local one is used until the game closes (`g_own`), with a notice: the game holds the save it read, and one
  read locally because the folder wasn't there yet went over the folder's newer one at the next save once it came back.
  A `.mhgu-commit` of `system` / `system_backup` is applied only at the save's full size (a sync app can bring one half-written).
  The in-use mark `.mhgu-in-use` is renewed every 5 minutes (`savefolder_tick`) and counts for 15.
- The settings menu holds the game when the player is alone (`[Application] PauseInMenu`, Android: `overlay.cpp` `menu_holds_game`,
  never with an LDN network up): the update thread stays in `overlay_frame`, drawing the menu (`frame_once`) and presenting the
  last picture again (`vk_present_again`, once the game's own presents stopped 50 ms ago), with the guest clock held and the
  presenting, vsync and audio threads waiting (`vi_menu_hold`, `vi_pause_wait`), as in the background. MH4U for Android does the
  same with its clock (`android_main.cpp`, `[Interface] PauseInMenu`).
- The two ports look and read alike (0.1.9's parity pass): the menu's tabs in the same order and names (Graphics & Audio,
  Display, Controls, Gameplay, Multiplayer, Mods, Cheats, Menu Options, Credits & Licenses: `GFX_PAGE`... in
  `settings_panel.cpp`), a phone's 3D resolution by Low-end mode and the frame rate (`scale_row`; a desktop's stays in
  Display by its Render resolution), the Touch controls / Controller and Your game / The app headers, Menu colours on
  phones too (listed Hunter, Gold, Blue; the ini keeps `THEME_*`), one FPS preset list (30, 60, 90, 120, 144, 240), the
  titles in the phone's Noto Serif (`overlay.cpp`'s second font, `title_font`; a desktop has its one font), Credits &
  Licenses as lines between the rows (`note`), a chevron on a row that goes on, the hunt buttons at half while a menu of
  the game's is open and those under the menu pad's tabs faded out with it (`under_mpad`), the hunt's d-pad (ShowDpad)
  too wherever it is, the menu pad taking its place (`hit` skips it meanwhile; 4u-native #25), the picked row's help on
  Buttons on the screen as on the tabs (`list_and_help`), the item bar's arrows on by
  default, one each side of the bar, as large as the room beside it allows (`item_arrow`; MH4U's `place_raw` has the
  same rule), and the same Spanish words (palanca, Envainar, Evasión, Guardia / correr, Diseño, barra de objetos). A name,
  a text or a default changed in one port is changed in the other (MH4U for Android: `app_native/settings_panel.cpp`,
  `settings_locale.h`, `touch.cpp`).
- Bugs that only show on some devices are common here. Test on a real phone, and on a device without Snapdragon.
