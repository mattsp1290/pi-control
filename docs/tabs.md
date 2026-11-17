# Add a tab

The tab bar is built from one ordered list, `tabs()` in `src/app/tabs.cpp`.
The Example tab (`src/app/example_page.h/.cpp`) is the template: a page
class, one list entry, one CMake line and one test.

## Steps

1. Add the page class in `src/app/<name>_page.h` and `.cpp`. It is a plain
   `QWidget`. Take `const AppContext&` in the constructor if the page needs
   shared services (Wi‑Fi, system stats, settings, the screensaver
   controller); the Example page needs none.

   ```cpp
   class WeatherPage final : public QWidget {
       Q_OBJECT
   public:
       explicit WeatherPage(const AppContext& context, QWidget* parent = nullptr);
   };
   ```

   Pages scroll with a finger when their content is taller than the screen:
   wrap the content in a `QScrollArea` and call
   `QScroller::grabGesture(scroll->viewport(), QScroller::LeftMouseButtonGesture)`,
   as `ExamplePage` does. Keep one scroller per page.

2. Add one entry to `tabs()` in `src/app/tabs.cpp`. The position in the list
   is the position on the tab bar; the id is lowercase and stable.

   ```cpp
   {"weather", "Weather", [](const AppContext& context) { return new WeatherPage(context); }},
   ```

3. Add the `.cpp` file to the `pi_control_app` source list in
   `CMakeLists.txt`.

4. Add a test. `tests/app_fixture.h` builds the whole shell at 800×480 with a
   fake Wi‑Fi backend; look the page up by its id:

   ```cpp
   AppFixture fixture;
   auto* weather = fixture.page<WeatherPage>(u"weather");
   QVERIFY(weather);
   ```

   Then update the expected titles in `tests/test_app_shell.cpp`. The smoke
   test reads the titles from `tabs()`, so it needs no change.

The touchscreen is 800×480 and the tab bar takes 64 px, so a page has about
416 px of height. Keep touch targets at least 56 px tall.

No change to `AppShell`, the tab bar style or the screensaver is needed: the
screensaver covers every tab, and its dismissing tap never reaches a page.
