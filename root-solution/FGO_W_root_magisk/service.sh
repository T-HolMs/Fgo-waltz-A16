#!/system/bin/sh
# ============================================================
#  FGO Waltz 修复模块 — Magisk late_start service
#
#  核心动作:给 app 域开放 ro.secure 所在属性区的读权限。
#
#  为什么放在这里而不是 sepolicy.rule:
#    实测本机 Magisk 30.7 没有应用模块根目录的 sepolicy.rule
#    (模块本身加载正常 —— 本脚本确实被执行了),因此改为在
#    service 阶段用 magiskpolicy --live 注入,已验证有效。
# ============================================================

# 等待开机完成
until [ "$(getprop sys.boot_completed)" = "1" ]; do
  sleep 1
done
sleep 3

LOG=/data/local/tmp/fgowaltz_fix.log
: > "$LOG"

{
  echo "=== FGO Waltz fix module ==="
  echo "date: $(date)"
  echo "selinux(before): $(getenforce)"
} >> "$LOG" 2>&1

# ------------------------------------------------------------
# 核心修复:放行 ro.secure 所在的属性区
#
# ro.secure 的 SELinux 类型是 userdebug_or_eng_prop。
# 正式版(user)系统上该类型没有给 app 域的 allow 规则,
# 导致属性区不被 mmap 进游戏进程,游戏读不到 ro.secure 而
# 以 ErrorCode=60 自杀(黑屏)。
# ------------------------------------------------------------
magiskpolicy --live "allow untrusted_app_29 userdebug_or_eng_prop file { open read map getattr }" >> "$LOG" 2>&1
echo "policy inject exit=$?" >> "$LOG"

# 保险:同时覆盖可能出现的其他 app 域别名
for D in untrusted_app untrusted_app_25 untrusted_app_27 untrusted_app_29; do
  magiskpolicy --live "allow $D userdebug_or_eng_prop file { open read map getattr }" >> "$LOG" 2>&1
done

# ------------------------------------------------------------
# 运行期权限(不影响游戏启动,只影响 AR/相机/通知等附加功能)
# ------------------------------------------------------------
PKG=com.aniplex.fgowaltz
appops set $PKG CAMERA allow                2>/dev/null
appops set $PKG READ_PHONE_STATE allow      2>/dev/null
appops set $PKG READ_EXTERNAL_STORAGE allow 2>/dev/null
appops set $PKG WRITE_EXTERNAL_STORAGE allow 2>/dev/null
appops set $PKG POST_NOTIFICATION allow     2>/dev/null

{
  echo "selinux(after): $(getenforce)"
  echo "done"
} >> "$LOG" 2>&1
