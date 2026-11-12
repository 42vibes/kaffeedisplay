# Browser-based preview using Docker

A desktop installation of ESP-IDF, LVGL, SDL2 and Node.js is **not required**.
With Docker Desktop or Docker Engine and Compose installed, from the repository root run:

```sh
docker compose -f compose.preview.yml up --build
```

Then open two **different** local views:

| URL | What is running |
|---|---|
| http://localhost:6080/vnc.html?autoconnect=1 | Actual 480 × 480 LVGL SDL **device simulator** in a browser via noVNC |
| http://localhost:5173 | React **admin web UI** with a separate mock server; admin demo password: `kaffee` |

Click inside the noVNC view to operate the simulated touchscreen. Keys `1`, `2`, `3` switch pages and `q` quits the simulator. Use the web UI to create and edit test users. Web and LVGL simulator use **separate** fake datasets; changes in the browser's mock admin are *not* mirrored on the SDL display.

Changes to the web UI are reflected via Vite's development server. Changes to the C++ LVGL UI require rebuilding the display container:

```sh
docker compose -f compose.preview.yml up --build display
```

To stop the preview, use Ctrl+C in the Compose terminal or run:

```sh
docker compose -f compose.preview.yml down
```

The VNC server has no password and is bound to **127.0.0.1** only. Never forward port 6080 to an external or public interface.

The **device simulator does not flash real hardware**. Tests of touch electronics, firmware boot, and PSRAM usage still need the ESP32 board.
