#!/bin/sh
set -eu

new_path="$1"
app_path="$2"
backup_path="${app_path}.backup"
log_path="/var/log/environment_monitor_ota_apply.log"
if ! touch "$log_path" 2>/dev/null; then
    log_path="/tmp/environment_monitor_ota_apply.log"
fi
exec >>"$log_path" 2>&1
echo "$(date 2>/dev/null || true) OTA apply started new=$new_path app=$app_path"

[ -f "$new_path" ] || exit 10
[ -x "$new_path" ] || chmod 0755 "$new_path"
rm -f "$backup_path"
cp -p "$app_path" "$backup_path"
mv -f "$new_path" "$app_path"
chmod 0755 "$app_path"
echo "binary replaced"

sync
sleep 1
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-linuxfb}"
startup_log="/var/log/environment_monitor_ota.log"
if ! touch "$startup_log" 2>/dev/null; then
    startup_log="/tmp/environment_monitor_ota.log"
fi
echo "starting new binary platform=$QT_QPA_PLATFORM log=$startup_log"
nohup "$app_path" </dev/null >"$startup_log" 2>&1 &
new_pid=$!
sleep 5
if kill -0 "$new_pid" 2>/dev/null; then
    echo "new binary is alive pid=$new_pid"
    exit 0
fi

echo "new binary exited before health check pid=$new_pid, rolling back"
mv -f "$backup_path" "$app_path"
rollback_log="/var/log/environment_monitor_ota_rollback.log"
if ! touch "$rollback_log" 2>/dev/null; then
    rollback_log="/tmp/environment_monitor_ota_rollback.log"
fi
nohup "$app_path" </dev/null >"$rollback_log" 2>&1 &
exit 20
