#!/bin/sh
set -eu

new_path="$1"
app_path="$2"
backup_path="${app_path}.backup"

[ -f "$new_path" ] || exit 10
[ -x "$new_path" ] || chmod 0755 "$new_path"
rm -f "$backup_path"
cp -p "$app_path" "$backup_path"
mv -f "$new_path" "$app_path"
chmod 0755 "$app_path"

if "$app_path" >/tmp/environment_monitor_ota.log 2>&1 & then
    new_pid=$!
    sleep 3
    if kill -0 "$new_pid" 2>/dev/null; then
        exit 0
    fi
fi

mv -f "$backup_path" "$app_path"
exec "$app_path" >/tmp/environment_monitor_ota_rollback.log 2>&1
