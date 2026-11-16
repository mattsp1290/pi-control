# Screensavers

After 15 seconds without input, Pi Control covers the shutdown screen with a
screensaver chosen uniformly at random from the built-in list. Any touch,
click, mouse movement, wheel or key press dismisses it. That input, and the
rest of the same gesture, never reaches the shutdown button. No screensaver
starts while a shutdown request is pending.

## Architecture

All code lives in `src/screensaver/` and builds into the
`pi_control_screensaver` static library.

| Component | Responsibility |
| --- | --- |
| `Screensaver` (`screensaver.h`) | Interface one animation implements: `start`, `resize`, time-based `advance`, `paint`. No timers, widgets or input. All randomness derives from the `start` seed. |
| Registry (`registry.h/.cpp`) | `builtinScreensavers()` lists every screensaver by id, name and factory. `--screensaver` help text and validation come from it. |
| `ScreensaverView` | Child widget covering the shutdown screen. Drives the active screensaver at about 30 Hz and logs frame statistics to stderr when dismissed. |
| `ScreensaverController` | Application-wide event filter. Owns the 15 s idle timer (`kIdleTimeout`), random selection, the wake guard and shutdown inhibition. |

Rendering is CPU-only: each screensaver draws into a reduced-resolution
`QImage` that is scaled to the screen. The Mandelbrot renderer uses every core
through `QtConcurrent`.

Animation runs for as long as the kiosk stays idle; there is no run limit.
Measured sustained load on a Raspberry Pi 4 (Release build, 1920×1080):
Mandelbrot about one core, water about half a core, Life about 6 % of a core.
Keep the Pi cooled accordingly and check `vcgencmd get_throttled` after a long
run.

The controller's states are `Watching`, `Active` and `WakeGuard`. A wake
event moves `Active` to `WakeGuard`, which consumes every input event until
all buttons, touch points and keys are released and at least 250 ms have
passed. A lost release cannot block new input for more than 3 s
(`kMaxWakeGuard`). If a press is still held when that limit expires, new
presses work again but the moves and release of the held press are still
swallowed. Keyboard shortcuts never fire while the screensaver or its guard
is active.

The screensaver does not start while a press is held: covering a press that a
widget has already seen would swallow its release and leave that widget
pressed. Activation waits for the release, for at most 60 s
(`kMaxHoldDeferral`) in case the release was lost.

## Add a screensaver

1. Add `src/screensaver/<id>.h` and `<id>.cpp` with a class implementing
   `Screensaver`. Keep simulation logic in a separate, widget-free class so it
   can be unit tested.
2. Add one entry to `builtinScreensavers()` in `src/screensaver/registry.cpp`.
   Ids are lowercase (`^[a-z][a-z0-9-]*$`) and unique.
3. Add the `.cpp` file to the `pi_control_screensaver` source list in
   `CMakeLists.txt`.
4. Add simulation tests to `tests/test_screensavers.cpp`. The registry-driven
   tests already check that every screensaver paints every pixel, is
   deterministic for a seed and survives empty and odd sizes.
5. Build the benchmark in Release and run it on a Raspberry Pi 4. Each
   screensaver should stay under 50 ms per frame at 1920×1080 (Debug builds
   are several times slower):

   ```sh
   cmake -S . -B build-bench -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build-bench --target screensaver-bench
   QT_QPA_PLATFORM=offscreen ./build-bench/screensaver-bench
   ```

6. Preview it: `./build/pi-control --dry-run --screensaver <id>`. It still
   starts after 15 seconds without input.

No controller, view or `main.cpp` change is needed.

## Diagnostics

The service writes stdout and stderr to the journal. Each activation logs
`Screensaver: <id> after <ms> ms idle`, and each dismissal of a screensaver
that ran at least 30 frames logs its frame count, median and p95 frame
interval:

```sh
journalctl -u pi-control -b | grep Screensaver
```
