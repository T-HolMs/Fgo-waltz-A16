# Fgo-waltz-A16

让已停服的手游 **Fate/Grand Order Waltz in the MOONLIGHT/LOSTROOM**(第三方复活版 `FGO W.apk`)
在 **Android 15 / 16** 上正常启动的修复方案。

原包只能在 Android 12 上运行,更高版本一律**黑屏闪退**。本仓库给出根因分析、
**免 root 修复方案**(native hook 库源码)、以及可选的 root 方案。

> 修复成品见 [Releases](../../releases)。

---

## 一、根因:一个读不到的系统属性

游戏启动时读取系统属性 **`ro.secure`**。

该属性的 SELinux 类型是 `userdebug_or_eng_prop`:

```
# 来自 plat_property_contexts
ro.secure  u:object_r:userdebug_or_eng_prop:s0  exact int
```

这个类型只存在于 **userdebug / eng 版本**的系统上。在正式版(user)系统里,
AOSP 中 `userdebug_or_eng()` 宏展开为空,于是 **app 域对它没有任何权限**:

```
avc: denied { read } for name="u:object_r:userdebug_or_eng_prop:s0"
    scontext=u:r:untrusted_app_29:s0  tclass=file  permissive=0
```

后果是**内核根本不会把这块属性区映射进游戏进程**。libc 的
`SystemProperties::Find()` 拿不到这个区,返回 `NULL`;游戏把「读不到 `ro.secure`」
判定为运行环境被篡改,直接 `abort()` —— 表现就是黑屏闪退。

**对照实验(同一台设备):**

| SELinux 状态 | 进程映射的属性区数 | 结果 |
|---|---|---|
| Enforcing | 164(不含 `userdebug_or_eng_prop`) | 黑屏 abort |
| Permissive | 591(含 `userdebug_or_eng_prop`) | **正常** |

在 Enforcing 下临时注入一条规则即可完全正常,这也是 [root 方案](./root-solution/) 的依据:

```
allow untrusted_app_29 userdebug_or_eng_prop file { open read map getattr }
```

---

## 二、免 root 方案:劫持属性区查找

既然无法让内核映射这块属性区,就**绕开「映射」这一步**:在游戏进程内部,
把 libc 用来获取属性区的那个**间接调用**替换掉。

### libc 的实现

`SystemProperties::Find` 的机器码形如:

```
ldrb w8, [x0, #0x490]     ; contexts_initialized_ 必须为 1
cbz  w8, fail
ldr  x0, [x0, #0x480]     ; self->contexts_
ldr  x8, [x0]             ; contexts_->vtable
ldr  x8, [x8, #0x18]      ; vtable[3]
blr  x8                   ; vtable[3](obj, name)  ->  prop_area*
      ... prop_area_find(area, name) ...
```

关键点:**目标是运行时从内存读出来的**,不是写死的。于是把
`self->contexts_` 的**虚表第 3 项**原地改掉,换成自己的函数:

```c
static void* my_vt3(void* obj, const char* name) {
    /* 命中伪造表:返回自建的 mini prop_area */
    /* 否则:转发给原来的 vtable[3] */
}
```

返回的 **mini `prop_area`** 按 libc 期望的格式手工构造。调用方拿到区之后,
只会用**同一个名字**再去查一次,所以一个区只装一个属性就够。

### mini prop_area 布局

偏移全部相对 `data_ = area + 0x80`,与真实属性区一致:

```
data_ + 0x10   root_off          (= 虚拟节点 area+0x80 的 child 字段)
data_ + 0x20   节点链,每段名字一个节点:
               +0x00 namelen   +0x04 prop_off  +0x08 left
               +0x0c right     +0x10 child     +0x14 name[namelen]
prop_off       prop_info: serial(len<<24) + value[92] + name
```

`child` / `prop` 都是相对 `data_` 的偏移,查找时 libc 会拿它和自己的
`bytes_used` 缓存值比较;我们的偏移远小于该值,因此必然通过边界检查。

这样即使内核**一个字节都没有映射**这块属性区,游戏也能读到
`ro.secure = "1"` 以及全部 Android 11 伪装属性。

---

## 三、为什么一定要走「改虚表」这条路

游戏带反篡改检测。实测结果:

| 改动方式 | 结果 |
|---|---|
| 改 libc **代码页**(替换 `__system_property_find` 的跳转) | 立刻 abort |
| 改代码页但**原样写回相同字节** | 立刻 abort |
| `mmap(MAP_FIXED)` 把代码页换成匿名副本 | 立刻 abort |
| 只往 libc `.bss` 写 **1 个字节** | 立刻 abort |
| **原地改虚表项(数据页,GNU_RELRO)** | **不触发检测,游戏正常** ✅ |

错误码对照:

| 错误码 | 含义 |
|---|---|
| `18593409` | 反篡改被触发 |
| `2` | 只缺伪装属性 |
| 进入标题画面 | 一切正常 |

所以本方案**只碰数据页,完全不改任何代码页** —— 这既是它能免 root 的原因,
也是它不被检测到的原因。

> 虚表位于 libc 的 `GNU_RELRO` 段(`r--p`)。加写只需 `mprotect`,不涉及
> `execmod`,与代码页的处境完全不同。注意**不能替换对象指针** ——
> `Find` 会把 `self->[0x480]` 的**值**当作 `obj` 传下去,换成自己的副本会因
> 尾部字段缺失而 SIGSEGV。必须**原地方改虚表项**。

---

## 四、兼容性

* **ABI**:`arm64-v8a`(真机)
* **系统**:Android 15 / 16 实测正常;**Android 17 理论可行**(机制一致,未实机验证)
* **不需要** root、Magisk、改 SELinux 或解锁
* **不依赖**任何外部配置文件:伪装表内置在库里,app 私有目录被清空后
  也能自动从 APK 重建

健壮性设计:

* `contexts_` 字段偏移(本机 `0x480`)是**运行时扫描** libc 指令流得出的,
  不是写死的,换系统版本能自适应;扫描失败时回退到内置默认值
* 属性区模板构造后逐条自检,不匹配会打 `MISMATCH` 日志
* hook 库会打印 `decoded` / `mini areas built` / `selftest` 等诊断日志

---

## 五、使用

### 直接安装成品

从 [Releases](../../releases) 下载 `FGO_W_noRoot.apk`。

1. **推荐覆盖安装**(签名与旧版一致):

   ```bash
   adb install -r FGO_W_noRoot.apk
   ```

   > ⚠️ **不要卸载旧版再装**:Android 11+ 下
   > `/sdcard/Android/data/com.aniplex.fgowaltz` 里是游戏的 849MB 数据,
   > 卸载会一并删除。

2. 启动游戏 → 标题画面 → 点一下 → 会提示「サーバーとの通信に失敗しました」
   (游戏已停服,属正常)→ 点「閉じる」进入离线模式。

### 从源码构建

需要 Android NDK(测试于 r27c):

```bash
aarch64-linux-android28-clang -shared -Oz -fPIC \
    -fno-unwind-tables -fno-asynchronous-unwind-tables \
    -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -o libfgowspoof_arm64.so src/fgowhook.c -llog -ldl
```

然后用 `tools/` 里的脚本替换 APK 内的 `assets/fgowboot/libfgowspoof_arm64.so`
并重新签名:

```powershell
python tools/repack_hook.py 原版.apk _hooked.apk
powershell tools/sign_only.ps1 -In _hooked.apk -Out FGO_W_noRoot.apk
```

> 替换后**必须重新签名**(zipalign → apksigner v1+v2+v3),
> 否则游戏的签名校验会失败。

---

## 六、调试开关

hook 库会读取 `/data/local/tmp/fgowhook.cfg`(存在才读)。
正常游玩不需要它,仅用于排查:

```bash
adb shell "echo trace=1 > /data/local/tmp/fgowhook.cfg"
adb logcat -s FGOwHook:*
```

| 开关 | 作用 |
|---|---|
| `trace=1` | 打印每个被查询的属性名、调用来源,并逐条自检伪装属性 |
| `areadump=1` | 打印返回的 `prop_area` 十六进制 |
| `area=0` | 关掉伪造(回到黑屏,用于对照) |
| `find=1` / `get=1` | 改代码页的旧方案 —— **会触发反篡改,仅用于验证结论** |

---

## 七、验证记录

**环境**:小米 HyperOS,Android 16(API 36),arm64,SELinux **Enforcing**,
**无 root 规则注入**(进程属性区映射数 166,不含 `userdebug_or_eng_prop`)。

* 进程内自检:`ro.secure -> "1"`;20 项伪装属性逐条比对,**0 处不符**
* 确认 `userdebug_or_eng_prop` **未映射**的情况下游戏照常运行
* 冷启动(整机重启)→ 标题画面 → 离线模式 → 3D 剧情场景,全程无 abort、无 ErrorCode
* app 私有目录清空后重放,自动重建,行为一致

---

## 八、可选:root 方案

如果设备已 root,可以直接注入 SELinux 规则,见 [root-solution/](./root-solution/):

```
allow untrusted_app_29 userdebug_or_eng_prop file { open read map getattr }
```

脚本会在开机后通过 `magiskpolicy --live` 注入,原理更直接,但需要 root。

---

## 九、免责声明

本项目仅用于**已停止运营**的游戏在新系统上的兼容性修复与技术研究。

游戏《Fate/Grand Order Waltz in the MOONLIGHT/LOSTROOM》的全部著作权归
**TYPE-MOON / Aniplex / DELiGHTWORKS** 所有。本仓库不包含游戏本体资源,
提供的补丁不改变游戏内容,也不用于任何商业用途。

请支持正版。如有侵权,请联系删除。
