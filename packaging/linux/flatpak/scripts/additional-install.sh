#!/bin/sh

# User Service
mkdir -p ~/.config/systemd/user
cp "/app/share/sunshine/systemd/user/sunshine.service" "$HOME/.config/systemd/user/sunshine.service"
echo "Sunshine User Service has been installed."
echo "Use [systemctl --user enable sunshine] once to autostart Sunshine on login."

# Load uhid (DS5 emulation)
UHID=$(cat /app/share/sunshine/modules-load.d/60-ares.conf)
echo "Enabling DS5 emulation."
flatpak-spawn --host pkexec sh -c "echo '$UHID' > /etc/modules-load.d/60-ares.conf"
flatpak-spawn --host pkexec modprobe uhid

# Udev rule
UDEV=$(cat /app/share/sunshine/udev/rules.d/60-ares.rules)
echo "Configuring mouse permission."
flatpak-spawn --host pkexec sh -c "echo '$UDEV' > /etc/udev/rules.d/60-ares.rules"
# Quoted rules would not survive the echo above; tee reads them from stdin.
flatpak-spawn --host pkexec tee /etc/udev/rules.d/74-ares-session-acl.rules > /dev/null < /app/share/sunshine/udev/rules.d/74-ares-session-acl.rules
echo "Restart computer for mouse permission to take effect."
