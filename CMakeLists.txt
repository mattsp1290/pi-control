cmake_minimum_required(VERSION 3.22)
project(pi_control VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(cmake/CPM.cmake)
CPMAddPackage("gh:fmtlib/fmt#11.1.4")
find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets DBus Concurrent Network)
set(CMAKE_AUTOMOC ON)
add_library(pi_control_screensaver STATIC
  src/screensaver/life.cpp
  src/screensaver/mandelbrot.cpp
  src/screensaver/registry.cpp
  src/screensaver/screensaver_controller.cpp
  src/screensaver/screensaver_view.cpp
  src/screensaver/water.cpp)
target_include_directories(pi_control_screensaver PUBLIC src)
target_link_libraries(pi_control_screensaver PUBLIC Qt6::Widgets PRIVATE Qt6::Concurrent fmt::fmt)
target_compile_options(pi_control_screensaver PRIVATE -Wall -Wextra -Wpedantic)
# Wi-Fi over NetworkManager; no widgets.
add_library(pi_control_wifi STATIC
  src/wifi/connect_attempt.cpp
  src/wifi/fake_wifi_backend.cpp
  src/wifi/nm_access_points.cpp
  src/wifi/nm_messages.cpp
  src/wifi/nm_wifi_backend.cpp
  src/wifi/wifi_backend.cpp)
target_include_directories(pi_control_wifi PUBLIC src)
target_link_libraries(pi_control_wifi PUBLIC Qt6::Core Qt6::DBus PRIVATE fmt::fmt)
target_compile_options(pi_control_wifi PRIVATE -Wall -Wextra -Wpedantic)
add_library(pi_control_app STATIC
  src/app/app_shell.cpp
  src/app/elided_label.cpp
  src/app/example_page.cpp
  src/app/home_page.cpp
  src/app/password_view.cpp
  src/app/power_panel.cpp
  src/app/screensaver_panel.cpp
  src/app/settings_page.cpp
  src/app/settings_store.cpp
  src/app/signal_bars.cpp
  src/app/tabs.cpp
  src/app/wifi_panel.cpp
  src/keyboard/on_screen_keyboard.cpp
  src/system/ip_addresses.cpp
  src/system/system_stats.cpp)
target_include_directories(pi_control_app PUBLIC src)
target_link_libraries(pi_control_app PUBLIC pi_control_screensaver pi_control_wifi Qt6::Widgets Qt6::DBus Qt6::Network
  PRIVATE fmt::fmt)
target_compile_options(pi_control_app PRIVATE -Wall -Wextra -Wpedantic)
add_executable(pi-control src/main.cpp)
target_link_libraries(pi-control PRIVATE pi_control_app fmt::fmt)
target_compile_options(pi-control PRIVATE -Wall -Wextra -Wpedantic)
include(GNUInstallDirs)
install(TARGETS pi-control RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
include(CTest)
if(BUILD_TESTING)
  find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
  add_executable(screensaver-tests tests/test_screensavers.cpp)
  target_link_libraries(screensaver-tests PRIVATE pi_control_screensaver Qt6::Test)
  target_compile_options(screensaver-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME screensaver-unit COMMAND screensaver-tests)
  set_tests_properties(screensaver-unit PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  add_executable(screensaver-bench tests/bench_screensavers.cpp)
  target_link_libraries(screensaver-bench PRIVATE pi_control_screensaver fmt::fmt)
  target_compile_options(screensaver-bench PRIVATE -Wall -Wextra -Wpedantic)
  add_executable(screensaver-controller-tests tests/test_screensaver_controller.cpp)
  target_link_libraries(screensaver-controller-tests PRIVATE pi_control_screensaver Qt6::Test)
  target_compile_options(screensaver-controller-tests PRIVATE -Wall -Wextra -Wpedantic)
  add_test(NAME screensaver-controller COMMAND screensaver-controller-tests)
  set_tests_properties(screensaver-controller PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  function(pi_control_qt_test name source)
    set(library pi_control_app)
    if(ARGC GREATER 2)
      set(library ${ARGV2})
    endif()
    add_executable(${name}-tests ${source})
    target_link_libraries(${name}-tests PRIVATE ${library} Qt6::Test)
    target_compile_options(${name}-tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME ${name} COMMAND ${name}-tests)
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
  endfunction()
  pi_control_qt_test(app-shell tests/test_app_shell.cpp)
  pi_control_qt_test(settings-page tests/test_settings_page.cpp)
  pi_control_qt_test(system-stats tests/test_system_stats.cpp)
  pi_control_qt_test(home-page tests/test_home_page.cpp)
  pi_control_qt_test(wifi-backend tests/test_wifi_backend.cpp pi_control_wifi)
  pi_control_qt_test(settings-store tests/test_settings_store.cpp)
  pi_control_qt_test(keyboard tests/test_on_screen_keyboard.cpp)
  pi_control_qt_test(wifi-panel tests/test_wifi_panel.cpp)
  add_test(NAME ui-dry-run COMMAND pi-control --smoke-test)
  set_tests_properties(ui-dry-run PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
  add_test(NAME shutdown-failure COMMAND pi-control --smoke-test-failure)
  set_tests_properties(shutdown-failure PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
  add_test(NAME reject-unknown-option COMMAND pi-control --unknown)
  set_tests_properties(reject-unknown-option PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" WILL_FAIL TRUE TIMEOUT 15)
  add_test(NAME reject-unknown-screensaver COMMAND pi-control --dry-run --screensaver nope)
  set_tests_properties(reject-unknown-screensaver PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    PASS_REGULAR_EXPRESSION "Unknown screensaver: nope" TIMEOUT 15)
  add_test(NAME accept-screensaver COMMAND pi-control --smoke-test --screensaver life)
  set_tests_properties(accept-screensaver PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/persisted-screensaver.ini "[screensaver]\nid=water\n")
  file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/persisted-screensaver-life.ini "[screensaver]\nid=life\n")
  add_test(NAME persisted-screensaver COMMAND ${CMAKE_COMMAND}
    "-DCMD=$<TARGET_FILE:pi-control>"
    "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/persisted-screensaver.ini"
    "-DEXPECT=Screensaver selection: water"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_and_expect.cmake)
  add_test(NAME persisted-screensaver-negative COMMAND ${CMAKE_COMMAND}
    "-DCMD=$<TARGET_FILE:pi-control>"
    "-DSETTINGS=${CMAKE_CURRENT_BINARY_DIR}/persisted-screensaver-life.ini"
    "-DEXPECT=Screensaver selection: water"
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_and_expect.cmake)
  set_tests_properties(persisted-screensaver persisted-screensaver-negative PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 15)
  set_tests_properties(persisted-screensaver-negative PROPERTIES WILL_FAIL TRUE)
endif()

option(PI_CONTROL_PACKAGE "Build a Debian deployment package" OFF)
if(PI_CONTROL_PACKAGE)
  if(NOT CMAKE_INSTALL_PREFIX STREQUAL "/usr")
    message(FATAL_ERROR "Debian packages require -DCMAKE_INSTALL_PREFIX=/usr")
  endif()
  file(READ deploy/pi-control.service service_unit)
  string(REPLACE "/usr/local/bin/pi-control" "/usr/bin/pi-control" service_unit "${service_unit}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/pi-control.service" "${service_unit}")
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pi-control.service" DESTINATION lib/systemd/system)
  install(FILES deploy/49-pi-control.rules DESTINATION share/polkit-1/rules.d)
  set(CPACK_GENERATOR DEB)
  set(CPACK_PACKAGE_NAME pi-control)
  set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
  set(CPACK_PACKAGE_CONTACT "Pi Control maintainers <mattsp1290@users.noreply.github.com>")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Fullscreen Raspberry Pi control screen")
  set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_DEPENDS "qt6-qpa-plugins, libegl1, libgles2, libgbm1, libinput10, polkitd, systemd, adduser")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_SOURCE_DIR}/distribution/debian/preinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/distribution/debian/postinst"
    "${CMAKE_CURRENT_SOURCE_DIR}/distribution/debian/prerm"
    "${CMAKE_CURRENT_SOURCE_DIR}/distribution/debian/postrm")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
  include(CPack)
endif()
