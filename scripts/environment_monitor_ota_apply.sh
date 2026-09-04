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

nohup "$app_path" </dev/null >/tmp/environment_monitor_ota.log 2>&1 &
new_pid=$!
sleep 5
if kill -0 "$new_pid" 2>/dev/null; then
    exit 0
fi

mv -f "$backup_path" "$app_path"
nohup "$app_path" </dev/null >/tmp/environment_monitor_ota_rollback.log 2>&1 &
exit 20
