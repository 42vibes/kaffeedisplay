#!/bin/sh
set -eu

export DISPLAY=:99
Xvfb :99 -screen 0 1024x768x24 -ac &
x11vnc -display :99 -forever -nopw -shared -rfbport 5900 -quiet &
websockify --web /usr/share/novnc 6080 127.0.0.1:5900 &

exec ./sim/build/kaffeekasse-sim
