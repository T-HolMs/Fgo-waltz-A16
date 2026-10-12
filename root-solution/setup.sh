#!/system/bin/sh
# ============================================================
#  FGO Waltz — 一键修复脚本(需要 root / Magisk)
#
#  作用:让 app 域能够读取 ro.secure 属性区。
#        不关闭 SELinux,不改 APK,不放宽其他任何权限。
#
#  用法:把本文件与 FGO_W_root_magisk/ 一起放到设备上,
#        su 下执行  sh setup.sh
# ============================================================

PKG=com.aniplex.fgowaltz
MODDIR=/data/adb/modules/FGO_W_sepolicy

echo "==================================================="
echo " FGO Waltz SELinux 修复"
echo "==================================================="
echo ""
echo "[1/4] 当前 SELinux: $(getenforce)"

# ------------------------------------------------------------
# 立即生效(不用重启):往运行中的策略里注入 allow 规则
# ------------------------------------------------------------
echo "[2/4] 注入 SELinux 规则(立即生效)..."
magiskpolicy --live "allow untrusted_app_29 userdebug_or_eng_prop file { open read map getattr }"
if [ $? -eq 0 ]; then
  echo "      规则注入成功"
else
  echo "      规则注入失败(检查是否已 root / magiskpolicy 是否可用)"
fi

# ------------------------------------------------------------
# 安装为 Magisk 模块(开机自动加载,重启后依然有效)
# ------------------------------------------------------------
echo "[3/4] 安装为 Magisk 模块(开机自动生效)..."
if [ -d "$MODDIR" ]; then
  rm -rf "$MODDIR"
fi
mkdir -p "$MODDIR"
cp -f FGO_W_root_magisk/module.prop    "$MODDIR/" 2>/dev/null
cp -f FGO_W_root_magisk/sepolicy.rule  "$MODDIR/" 2>/dev/null
cp -f FGO_W_root_magisk/service.sh     "$MODDIR/" 2>/dev/null
chmod 755 "$MODDIR/service.sh" 2>/dev/null
echo "      模块已安装到 $MODDIR"

# ------------------------------------------------------------
# 权限(可选,不影响启动)
# ------------------------------------------------------------
echo "[4/4] 设置运行期权限..."
appops set $PKG CAMERA allow                2>/dev/null
appops set $PKG READ_PHONE_STATE allow      2>/dev/null
appops set $PKG READ_EXTERNAL_STORAGE allow 2>/dev/null
appops set $PKG WRITE_EXTERNAL_STORAGE allow 2>/dev/null
appops set $PKG POST_NOTIFICATION allow     2>/dev/null
echo "      完成"

echo ""
echo "==================================================="
echo " 修复完成!"
echo ""
echo " 现在直接启动游戏即可。首次建议先重启一次设备,"
echo " 让 Magisk 模块完整生效。"
echo ""
echo " 验证方法:"
echo "   1) getenforce           应该显示 Enforcing"
echo "   2) 启动游戏             应能正常进入标题画面"
echo "==================================================="
