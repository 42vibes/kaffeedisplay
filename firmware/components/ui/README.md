# Device UI component

LVGL 9 implementation of the 480×480, German, espresso-themed kiosk UI. The
frozen entry point is `include/kaffee/ui.hpp`; everything below is the
integration seam for `firmware/main` (owned by T-009).

## Boot order

```cpp
// 1. Board: panel/touch/backlight, then LVGL through esp_lvgl_port.
auto display = kaffee::EspDisplay::create();
display->init();
display->init_lvgl();

// 2. Adapt the board to the UI display port and attach it. The stay-awake
//    callback must reach the board's own inactivity/deadline tracking.
kaffee::ui::BoardDisplayPort port(
    *display, display->lv_display(),
    [&](bool on) { display->set_stay_awake(on); });  // board PR #9 API
kaffee::ui::attach_display_port(&port);

// 3. Service/platform construction (T-001/T-005/T-009).
auto service = kaffee::KaffeeService::create(store, settings_store, platform);

// 4. UI creation/start must run on the LVGL task (inside display->lock()).
auto app = kaffee::ui::UiApp::create(*service);
app->start();
```

`BoardDisplayPort` (`include/kaffee/ui_board_adapter.hpp`) is header-only and
uses only `IDisplay`, so the UI implementation never includes board or ESP-IDF
headers. `UiApp::create()` remains exactly as frozen in `ui.hpp`.

## Runtime contract

- `UiApp::refresh_data()` and `UiApp::start()` are LVGL-thread calls.
- `IUiBackend::set_change_callback()` is invoked from service/HTTP threads; the
  UI callback only pushes a flag into a mutex-protected queue. `tick()` (or the
  internal 100 ms `lv_timer`) drains it on the LVGL thread.
- `UiApp::wake()` is safe to call from the platform for touch/backlight wake.
- While the admin AP is active, the UI calls `DisplayPort::set_stay_awake(true)`
  and clears it when the AP stops. `lv_display_trigger_activity()` alone does not
  update the board's own last-activity clock.
- The UI is read-only for display settings; `apply_display_settings` stays in
  the service/admin path.

## Verification status

The simulator (`sim/`) drives the real UI and domain code. Hardware boot
verification is pending T-009 wiring; see `docs/tasks/T-003-device-ui.md`.
