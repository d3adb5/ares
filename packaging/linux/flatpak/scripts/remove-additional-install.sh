#!/bin/sh

# User Service
systemctl --user stop sunshine
rm "$HOME/.config/systemd/user/sunshine.service"
systemctl --user daemon-reload
echo "Sunshine User Service has been removed."

# Remove rules
flatpak-spawn --host pkexec sh -c "rm /etc/modules-load.d/60-ares.conf"
flatpak-spawn --host pkexec sh -c "rm /etc/udev/rules.d/60-ares.rules /etc/udev/rules.d/74-ares-session-acl.rules"
echo "Input rules removed. Restart computer to take effect."
