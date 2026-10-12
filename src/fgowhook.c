/*
 * fgowhook.c - FGO Waltz 在 Android 16/17 上的免 root 修复库
 *
 * 问题:
 *   游戏启动时读取系统属性 ro.secure。
 *   /system/etc/selinux/plat_property_contexts 里它的类型是
 *     ro.secure  u:object_r:userdebug_or_eng_prop:s0  exact int
 *   AOSP 用 userdebug_or_eng() 宏包住该类型,正式版(user)系统上宏
 *   展开为空,因此 app 域没有读权限 -> 该属性区不会被 mmap 进游戏
 *   进程 -> bionic 的 SystemProperties::Find 返回 NULL 并打印
 *     "Access denied finding property "ro.secure""
 *   -> 游戏以 ErrorCode = 60 自杀 -> 黑屏。
 *
 * 本库做什么:
 *   在 System.load 时于游戏进程内 hook libc 的两个转发 thunk
 *     __system_property_find / __system_property_get
 *   对 ro.secure 直接返回 "1";其它被伪装表覆盖的属性返回伪造值。
 *   同时保留原 spoof 库的 Android 11 伪装行为。
 *
 * 为什么 hook 这两个 thunk:
 *   二者是 libc 的公开导出入口,函数体只有一次 tail-call:
 *     __system_property_find:  bti c / mov x1,x0 / nop / adr x0,&obj / b <impl>
 *     __system_property_get:   bti c / mov x2,x1 / mov x1,x0 / nop / adr x0,&obj / b <impl>
 *   入口前 16 字节内没有任何 PC 相对指令需要在别处重定位,所以直接
 *   覆盖入口即可,既不需要 trampoline 也不会像改 PLT stub 那样与
 *   原 spoof 库打架。真实现地址与 SystemProperties 对象地址全部在
 *   运行时从 thunk 机器码里解码,不依赖任何硬编码偏移。
 */

#define _GNU_SOURCE
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <link.h>
#include <sys/uio.h>
#include <android/log.h>

#define TAG "FGOwHook"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

/* prop_info 布局(实测自 libc 0x70760 `add x1,x19,#0x60`):
 *   +0   serial (u32)
 *   +4   value[PROP_VALUE_MAX=92]
 *   +96  name[]
 * serial 的 bits 24..31 存放值的长度(实测 libc 0x7072c `lsr w20,w25,#24`
 * 后直接 `mov w0,w20` 返回),bits 0..23 保留原模板的标志位。
 */
#define PROP_VALUE_MAX 92
#define PROP_NAME_OFF  96
#define FAKE_MAX       256

typedef const void* prop_info_t;
typedef prop_info_t (*find_impl_t)(void* self, const char* name);
typedef int         (*get_impl_t)(void* self, const char* name, char* value);

static void*       g_sysprops  = NULL;
static find_impl_t g_real_find = NULL;
static get_impl_t  g_real_get  = NULL;

/* 运行时可切换开关(从 /data/local/tmp/fgowhook.cfg 读取),便于无需重新打包定位问题。
 * 交付默认值:只走虚表劫持 + 自建 mini area(纯数据路径)。
 * 任何对 libc 代码页的改写都会被游戏的反篡改检测到并 abort,因此 find/get 必须关闭。 */
static int g_opt_find   = 0;   /* 是否 hook __system_property_find(会改代码页,禁用) */
static int g_opt_get    = 0;   /* 是否 hook __system_property_get (会改代码页,禁用) */
static int g_opt_spoof  = 1;   /* 是否对 spoof 表里的属性伪造 */
static int g_opt_secure = 1;   /* 是否伪造 ro.secure */
static int g_trace      = 0;   /* 是否记录调用者 */
static int g_trace_cnt  = 0;
static int g_opt_noop   = 0;   /* 只替换页写回原字节,用于区分"页替换副作用"与"字节被检测" */
static int g_opt_vt     = 0;   /* 劫持 self->[0x480] 虚表对象(纯数据路径,不碰代码页) */
static int g_opt_poke   = 0;   /* 只往 libc 的 bss 写一个字节,验证"碰 libc 是否必崩" */
static int g_opt_vteq   = 0;   /* 等价替换:换对象指针但虚表内容不变,区分"检测"与"功能破坏" */

/* 伪装表:保持与原 fgowspoof 一致(游戏需要看到 Android 11),
 * 额外加入 ro.secure —— 这是本次修复的真正目标。 */
typedef struct { const char* name; const char* value; } spoof_ent_t;

static const spoof_ent_t g_default[] = {
    { "ro.build.version.release",                        "11"   },
    { "ro.build.version.release_or_codename",            "11"   },
    { "ro.build.version.preview_sdk",                    "0"    },
    { "ro.build.version.preview_sdk_fingerprint",        "REL"  },
    { "ro.build.version.codename",                       "REL"  },
    { "ro.build.version.all_codenames",                  "REL"  },
    { "ro.build.version.min_supported_target_sdk",       "23"   },
    { "ro.product.build.version.release",                "11"   },
    { "ro.product.build.version.release_or_codename",    "11"   },
    { "ro.system.build.version.release",                 "11"   },
    { "ro.system.build.version.release_or_codename",     "11"   },
    { "ro.system_ext.build.version.release",             "11"   },
    { "ro.system_ext.build.version.release_or_codename", "11"   },
    { "ro.vendor.build.version.release",                 "11"   },
    { "ro.vendor.build.version.release_or_codename",     "11"   },
    { "ro.odm.build.version.release",                    "11"   },
    { "ro.odm.build.version.release_or_codename",        "11"   },
    { "ro.vndk.version",                                 "30"   },
    { "ro.vndk.lite",                                    "true" },
    { "ro.secure",                                       "1"    },
    { NULL, NULL }
};

#define MAX_ENTS 64

struct ent_t {
    char    name[128];
    char    value[128];
    int     ready;
    uint8_t pi[FAKE_MAX] __attribute__((aligned(16)));
};

static struct ent_t g_ents[MAX_ENTS];
static int g_nents = 0;

/*
 * 自建 mini prop_area。
 *
 * 有 SELinux 授权时 userdebug_or_eng_prop 会被映射进进程,没有时 SystemProperties::
 * Find 返回 NULL —— 我们无法凭空让内核映射它,所以改为在 vtable[3] 这一层直接把
 * 调用方要的属性当作"存在于我们自己的 area 里"返回。调用方拿到 area 后只会用
 * 同一个名字去查,因此一个 area 装一个属性就够。
 *
 * 布局(偏移全部相对 data_ = area + 0x80,与真实 area 一致):
 *   data_+0x10    root_off(等价于虚拟节点 area+0x80 的 child 字段)
 *   data_+0x20    节点链:每段名字一个节点
 *                 +0x00 namelen  +0x04 prop_off  +0x08 left
 *                 +0x0c right    +0x10 child     +0x14 name[namelen]
 *   prop_off      prop_info:serial(len<<24) + value[92] + name
 * child / prop 偏移均相对 data_,查找时 libc 会与它自己缓存的 bytes_used 比较,
 * 我们的偏移远小于该值,因此必然通过边界检查。
 */
#define AREA_SIZE 1024

static uint8_t g_areas[MAX_ENTS][AREA_SIZE] __attribute__((aligned(64)));
static int     g_area_ready[MAX_ENTS];

static int build_area(int idx, const char* name, const char* value) {
    uint8_t* a = g_areas[idx];
    uint8_t* data;
    const char* seg[16];
    uint32_t seglen[16], nodeoff[16];
    int nseg = 0, i;
    uint32_t off, prop_off, used, serial, ver = 0xfc6ed0ab, zero = 0;
    size_t vlen = strlen(value), nlen = strlen(name);
    const char* p = name;

    if (0x80 + 0x40 + nlen + 0x60 + nlen + 1 > AREA_SIZE) return -1;

    for (;;) {
        const char* dot = strchr(p, '.');
        if (nseg >= 16) return -2;
        seg[nseg]    = p;
        seglen[nseg] = (uint32_t)(dot ? (size_t)(dot - p) : strlen(p));
        nseg++;
        if (!dot) break;
        p = dot + 1;
    }

    memset(a, 0, 0x80);
    memcpy(a + 8, "PROP", 4);
    memcpy(a + 0xc, &ver, 4);

    off = 0x20;
    for (i = 0; i < nseg; i++) {
        nodeoff[i] = off;
        off += 0x14 + seglen[i];
        off = (off + 3) & ~3u;
    }
    prop_off = off;
    used = prop_off + 0x60 + (uint32_t)nlen + 1;

    data = a + 0x80;
    for (i = 0; i < nseg; i++) {
        uint8_t* n = data + nodeoff[i];
        uint32_t nl    = seglen[i];
        uint32_t child = (i + 1 < nseg) ? nodeoff[i + 1] : 0;
        uint32_t prop  = (i + 1 == nseg) ? prop_off : 0;
        memcpy(n + 0x00, &nl, 4);
        memcpy(n + 0x04, &prop, 4);
        memcpy(n + 0x08, &zero, 4);
        memcpy(n + 0x0c, &zero, 4);
        memcpy(n + 0x10, &child, 4);
        memcpy(n + 0x14, seg[i], nl);
    }
    memcpy(data + 0x10, &nodeoff[0], 4);

    {
        uint8_t* pi = data + prop_off;
        serial = (uint32_t)vlen << 24;
        memset(pi, 0, 0x60 + nlen + 1);
        memcpy(pi + 0x00, &serial, 4);
        memcpy(pi + 0x04, value, vlen + 1);
        memcpy(pi + PROP_NAME_OFF, name, nlen + 1);
    }
    memcpy(a, &used, 4);

    g_area_ready[idx] = 1;
    LOGI("area[%d] '%s' = '%s' nseg=%d used=0x%x", idx, name, value, nseg, used);
    return 0;
}

static const char* g_conf_paths[] = {
    "/data/local/tmp/fgowspoof.txt",
    "/sdcard/fgowspoof.txt",
    "/data/user/0/com.aniplex.fgowaltz/files/fgowspoof.txt",
    "/data/data/com.aniplex.fgowaltz/files/fgowspoof.txt",
    NULL
};

/* ---------------------------------------------------------------- 机器码解码 */

/* 解码 adr Xd, <label>:0x10000000 | immlo:2 | immhi:19 */
static void* decode_adr(const void* at) {
    uint32_t insn;
    memcpy(&insn, at, 4);
    if ((insn & 0x9F000000u) != 0x10000000u) return NULL;
    uint64_t immlo = (insn >> 29) & 0x3u;
    uint64_t immhi = (insn >> 5)  & 0x7FFFFu;
    int64_t  imm   = (int64_t)((immhi << 2) | immlo);
    if (imm & (1 << 20)) imm |= ~((int64_t)(1 << 21) - 1);
    return (void*)((uintptr_t)at + imm);
}

/* 解码 b <label>:0x14000000 | imm26 */
static void* decode_b(const void* at) {
    uint32_t insn;
    memcpy(&insn, at, 4);
    if ((insn & 0xFC000000u) != 0x14000000u) return NULL;
    int64_t imm = (int64_t)(insn & 0x03FFFFFFu);
    if (imm & (1 << 25)) imm |= ~((int64_t)(1 << 26) - 1);
    imm <<= 2;
    return (void*)((uintptr_t)at + imm);
}

/* ---------------------------------------------------------------- 改写代码页 */

/*
 * 直接改写 libc 只读代码页。
 *
 * 不能用 mprotect 加写权限:对文件映射的代码页那需要 execmod 权限,app 域
 * 没有(这正是必须免 root 解决的根源)。而用 mmap(MAP_FIXED) 把整页换成匿名
 * 可写副本虽然是原 spoof 库的手法,却会让同一页上其它 thunk 在替换窗口内
 * 失去可执行性(实测 RenderThread 在 vulkan 初始化时遍历属性,执行到同页的
 * __system_property_read_callback 就 SIGSEGV/SEGV_ACCERR)。
 *
 * 改用 /proc/self/mem:pwrite 到这个调试接口写只读代码页是合法的(内核按
 * FOLL_FORCE 处理),页面映射属性完全不变,始终是可执行的。
 */
static int write_code(void* addr, const void* data, size_t len) {
    int fd = open("/proc/self/mem", O_RDWR | O_CLOEXEC);
    ssize_t n;
    int err;
    if (fd < 0) {
        LOGE("open /proc/self/mem failed: fd=%d errno=%d", fd, errno);
        return -1;
    }
    n = pwrite(fd, data, len, (off_t)(uintptr_t)addr);
    err = errno;
    close(fd);
    if (n != (ssize_t)len) {
        LOGE("pwrite /proc/self/mem failed: n=%ld errno=%d", (long)n, err);
        return -2;
    }
    return 0;
}

static void dump_perm(const char* tag, void* addr) {
    FILE* f = fopen("/proc/self/maps", "re");
    char line[512];
    if (!f) { LOGE("%s: maps open failed", tag); return; }
    while (fgets(line, sizeof(line), f)) {
        unsigned long lo = 0, hi = 0;
        char perm[8] = {0};
        if (sscanf(line, "%lx-%lx %7s", &lo, &hi, perm) == 3) {
            if ((unsigned long)addr >= lo && (unsigned long)addr < hi) {
                LOGI("%s %p perm=%s", tag, addr, perm);
                fclose(f);
                return;
            }
        }
    }
    fclose(f);
    LOGE("%s %p: no mapping found", tag, addr);
}

/* 备选 2:process_vm_writev —— 走 ptrace 检查而非 file:open,可能绕过 /proc/self/mem 的拒绝。
 * 关键优势:完全不改变目标页的映射属性。 */
static int write_code_pvm(void* addr, const void* data, size_t len) {
    struct iovec local, remote;
    ssize_t n;
    local.iov_base = (void*)data;
    local.iov_len = len;
    remote.iov_base = addr;
    remote.iov_len = len;
    n = process_vm_writev(getpid(), &local, 1, &remote, 1, 0);
    if (n != (ssize_t)len) {
        LOGE("process_vm_writev failed: n=%ld errno=%d", (long)n, errno);
        return -1;
    }
    return 0;
}

/*
 * 从 SystemProperties::Find 的指令流里提取 contexts_ 字段偏移。
 *
 * libc 的实现形如:
 *     ldrb w8, [x0, #INIT]      ; contexts_initialized_
 *     cbz  w8, fail
 *     ldr  x0, [x0, #CTX]       ; self->contexts_
 *     ldr  x8, [x0]             ; contexts_->vtable
 *     ...
 *     blr  x8                   ; vtable[3](obj, name)
 * 这个偏移随 AOSP 版本变化(本机 Android 16 是 0x480),写死会在别的版本上失效,
 * 所以扫描 Find 的前若干条指令取出第一个 ldr x0,[x0,#imm] 的 imm。
 */
static uint32_t scan_ctx_off(const void* impl) {
    const uint32_t* p = (const uint32_t*)impl;
    int i;
    if (!impl) return 0;
    for (i = 0; i < 64; i++) {
        uint32_t insn = p[i];
        if ((insn & 0xFFFFFFE0u) == 0xF9400000u) {   /* ldr x0, [x0, #imm] */
            uint32_t imm = ((insn >> 10) & 0xFFFu) * 8u;
            if (imm >= 0x100 && imm <= 0x1000) return imm;
        }
    }
    return 0;
}

/* 兜底:把整页 MAP_FIXED 换成匿名可写副本(仅在其它方式都不可用时用) */
static int write_code_mapfixed(void* addr, const void* data, size_t len) {
    uintptr_t page = (uintptr_t)addr & ~(uintptr_t)0xFFF;
    size_t off = (uintptr_t)addr - page;
    uint8_t* backup = (uint8_t*)malloc(4096);
    void* r;
    if (!backup) return -1;
    memcpy(backup, (void*)page, 4096);
    r = mmap((void*)page, 4096, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (r == MAP_FAILED) { free(backup); return -2; }
    memcpy((void*)page, backup, 4096);
    free(backup);
    memcpy((uint8_t*)page + off, data, len);
    if (mprotect((void*)page, 4096, PROT_READ | PROT_EXEC) != 0) return -3;
    __builtin___clear_cache((char*)page, (char*)page + 4096);
    return 0;
}

/* 把 thunk 入口 16 字节改写为:ldr x17,#8 ; br x17 ; <64 位目标地址> */
static int patch_thunk(void* thunk, void* target) {
    uint32_t code[4];
    uint64_t t = (uint64_t)target;
    int rc;
    code[0] = 0x58000051u;               /* ldr x17, #8  */
    code[1] = 0xD61F0220u;               /* br  x17      */
    memcpy(&code[2], &t, 8);
    if (g_opt_noop) memcpy(code, thunk, 16);   /* 写回原字节 */

    rc = write_code(thunk, code, 16);
    if (rc != 0) {
        rc = write_code_pvm(thunk, code, 16);
        if (rc == 0) {
            LOGI("patched via process_vm_writev %p", thunk);
        } else {
            LOGE("MAP_FIXED fallback (page mapping will change!) for %p", thunk);
            dump_perm("before-mapfixed", thunk);
            rc = write_code_mapfixed(thunk, code, 16);
            dump_perm("after-mapfixed ", thunk);
        }
    } else {
        dump_perm("after-memwrite  ", thunk);
    }
    __builtin___clear_cache((char*)thunk, (char*)thunk + 16);
    return rc;
}

/* ---------------------------------------------------------------- hook 本体 */

static int find_ent(const char* name) {
    int i;
    if (!name) return -1;
    for (i = 0; i < g_nents; i++) {
        if (g_ents[i].name[0] == name[0] && strcmp(g_ents[i].name, name) == 0)
            return i;
    }
    return -1;
}

static void trace_caller(const char* tag, const char* name, void* ra) {
    Dl_info di;
    if (g_trace_cnt >= 400) return;
    g_trace_cnt++;
    if (dladdr(ra, &di) && di.dli_fname)
        LOGI("%s '%s' from %s+0x%lx", tag, name ? name : "(null)",
             di.dli_fname, (unsigned long)((char*)ra - (char*)di.dli_fbase));
    else
        LOGI("%s '%s' from %p", tag, name ? name : "(null)", ra);
}

__attribute__((noinline, used))
static prop_info_t hook_find(const char* name) {
    int i;
    if (g_trace) trace_caller("FIND", name, __builtin_return_address(0));
    i = find_ent(name);
    if (i >= 0 && g_ents[i].ready) {
        int is_sec = (strcmp(g_ents[i].name, "ro.secure") == 0);
        if ((is_sec && g_opt_secure) || (!is_sec && g_opt_spoof))
            return (prop_info_t)g_ents[i].pi;
    }
    return g_real_find(g_sysprops, name);
}

__attribute__((noinline, used))
static int hook_get(const char* name, char* value) {
    int i;
    if (g_trace) trace_caller("GET ", name, __builtin_return_address(0));
    i = find_ent(name);
    if (i >= 0 && g_ents[i].ready) {
        int is_sec = (strcmp(g_ents[i].name, "ro.secure") == 0);
        if ((is_sec && g_opt_secure) || (!is_sec && g_opt_spoof)) {
            size_t len = strlen(g_ents[i].value);
            if (value) memcpy(value, g_ents[i].value, len + 1);
            return (int)len;
        }
    }
    return g_real_get(g_sysprops, name, value);
}

/* ---------------------------------------------------------------- 伪造 prop_info */

/*
 * 克隆模板 prop_info 并把值换成 newval;new_name 覆盖克隆来的 name。
 * ro.secure 用同前缀、同值长度的 ro.adb.secure 作模板改名而来,
 * 这样 serial 的编码天然合法。
 */
static int build_fake(int idx, const char* tmpl_name, const char* new_name,
                      const char* newval) {
    prop_info_t tmpl = g_real_find(g_sysprops, tmpl_name);
    size_t name_len, vlen;
    uint32_t serial;

    if (!tmpl) return -1;

    name_len = strlen(new_name) + 1;
    memcpy(g_ents[idx].pi, tmpl, PROP_NAME_OFF + name_len);
    memcpy(g_ents[idx].pi + PROP_NAME_OFF, new_name, name_len);

    vlen = strlen(newval);
    memset(g_ents[idx].pi + 4, 0, PROP_VALUE_MAX);
    memcpy(g_ents[idx].pi + 4, newval, vlen + 1);

    /* serial 的 bits24..31 是值长度,低 24 位保留模板的标志位 */
    memcpy(&serial, g_ents[idx].pi, 4);
    serial = (serial & 0x00FFFFFFu) | ((uint32_t)vlen << 24);
    memcpy(g_ents[idx].pi, &serial, 4);

    g_ents[idx].ready = 1;
    return 0;
}

/* ---------------------------------------------------------------- 虚表劫持 */

/*
 * SystemProperties::Find / Get 的实际查找都是:
 *     x  = self->[0x480]->vtable[3](obj, name);   <- 间接调用,目标来自内存
 *     pi = prop_area_find(x, name);
 * self 位于 libc 的 [anon:.bss](rw-p,可直接写),因此只需把 self->[0x480]
 * 换成我们自己的对象+虚表即可拦截,完全不需要改任何代码页 —— 这是绕过
 * 游戏反篡改的关键(实测:改代码页会被检测并 abort,改数据则不会)。
 */
#define OFF_FINDER 0x480
static uint32_t g_ctx_off = OFF_FINDER;   /* contexts_ 字段偏移,运行时扫描得出 */

/* Template of the userdebug_or_eng_prop property area (560 bytes).
 * Every field inside is an area-relative offset, so the whole blob can be
 * handed to prop_area lookup as-is from any writable memory.
 * prop_info(ro.secure) sits at +0x1b4: serial=0x01000000 (value length 1),
 * value="1", name@+0x214="ro.secure".
 * Without SELinux permission this area is never mapped into the app process,
 * SystemProperties::Find returns NULL, so vtable[3] returns this template.
 */
static const unsigned char g_area_tmpl[560] __attribute__((aligned(64))) = {
    0xa0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x52, 0x4f, 0x50,
    0xab, 0xd0, 0x6e, 0xfc, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x70, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x88, 0x00, 0x00, 0x00, 0x72, 0x6f, 0x00, 0x00,
    0x0a, 0x00, 0x00, 0x00, 0xa8, 0x00, 0x00, 0x00, 0x18, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x65, 0x62, 0x75,
    0x67, 0x67, 0x61, 0x62, 0x6c, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
    0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x72, 0x6f, 0x2e, 0x64,
    0x65, 0x62, 0x75, 0x67, 0x67, 0x61, 0x62, 0x6c, 0x65, 0x00, 0x00, 0x00,
    0x06, 0x00, 0x00, 0x00, 0x34, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x73, 0x65, 0x63, 0x75,
    0x72, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x31, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x72, 0x6f, 0x2e, 0x73, 0x65, 0x63, 0x75, 0x72,
    0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static void*    g_orig_obj  = NULL;
static void*  (*g_orig_vt3)(void*, const char*) = NULL;
static int      g_area_dump = 0;   /* dump vtable[3] 返回的 prop_area 结构 */
static int      g_area_cnt  = 0;
static int      g_opt_tmpl  = 0;   /* ro.secure 返回自建 area 模板 */
static int      g_opt_area  = 1;   /* 用自建 mini area 伪造全部属性(交付默认) */
static int      g_opt_tmplall = 0; /* 所有属性都返回模板(诊断用) */

/* 需要伪造的属性。这些属性在正式版系统上属于 userdebug_or_eng_prop 类型,
 * app 域读不到,必须由我们自己给出一个可解析的 area。 */
static int need_fake(const char* n) {
    if (!n) return 0;
    if (g_opt_tmplall) return 1;
    return !strcmp(n, "ro.secure");
}

__attribute__((noinline, used))
static void* my_vt3(void* obj, const char* name) {
    static __thread int in_hook = 0;
    void* area;
    /* LOGI 会经 liblog 再次回调本函数,必须防递归 */
    if (in_hook) return g_orig_vt3(g_orig_obj, name);
    in_hook = 1;
    if (g_opt_area) {
        int ai = find_ent(name);
        if (ai >= 0 && g_area_ready[ai]) {
            in_hook = 0;
            (void)obj;
            return (void*)g_areas[ai];
        }
    }
    if (g_opt_tmpl && need_fake(name)) {
        in_hook = 0;
        (void)obj;
        return (void*)(uintptr_t)g_area_tmpl;
    }
    area = g_orig_vt3(g_orig_obj, name);
    if (g_area_dump && g_area_cnt < 40) {
        g_area_cnt++;
        LOGI("VT3 name='%s' -> area=%p", name ? name : "(null)", area);
        if (area && g_area_cnt <= 4) {
            unsigned int i;
            char line[3 * 16 + 1];
            unsigned int lim = 0x1c0 / 16;   /* 覆盖整个 bytes_used(0x1a0) */
            for (i = 0; i < lim; i++) {
                unsigned char* p = (unsigned char*)area + i * 16;
                int k;
                for (k = 0; k < 16; k++) {
                    static const char* hx = "0123456789abcdef";
                    line[k * 3]     = hx[(p[k] >> 4) & 0xF];
                    line[k * 3 + 1] = hx[p[k] & 0xF];
                    line[k * 3 + 2] = (k == 15) ? '\0' : ' ';
                }
                LOGI("  +%03x: %s", i * 16, line);
            }
        }
    }
    in_hook = 0;
    (void)obj;
    return area;
}

/*
 * 只替换虚表第 3 项(ContextsSerialized::GetPropAreaForName),不改对象指针。
 *
 * 关键:Find/Get 会把 self->[0x480] 的*值*当作 obj 传给该虚函数,所以若把
 * 指针换成自己的副本,就必须提供完整对象(否则尾部字段缺失会 SIGSEGV,
 * 实测已踩过这个坑)。改成原地改虚表项后,obj 仍是原始对象,字段完整。
 *
 * 虚表位于 libc 的 GNU_RELRO 段(r--p)。这是数据页,加写只需 mprotect,
 * 不触发 execmod —— 与代码页的处境完全不同。
 */
static int install_vthijack(void) {
    void** slot;
    void* obj;
    void* vt;
    uintptr_t page;
    if (!g_sysprops) return -1;
    slot = (void**)((char*)g_sysprops + g_ctx_off);
    obj = *slot;
    if (!obj) { LOGE("vt: finder slot is NULL"); return -2; }
    vt = *(void**)obj;
    if (!vt) { LOGE("vt: vtable is NULL"); return -3; }

    page = (uintptr_t)vt & ~(uintptr_t)0xFFF;
    if (mprotect((void*)page, 0x1000, PROT_READ | PROT_WRITE) != 0) {
        LOGE("vt: mprotect RELRO page %p failed errno=%d", (void*)page, errno);
        return -4;
    }
    g_orig_vt3 = (void*)(uintptr_t)((uint64_t*)vt)[3];
    g_orig_obj = obj;
    if (!g_opt_vteq) ((uint64_t*)vt)[3] = (uint64_t)(uintptr_t)&my_vt3;
    if (mprotect((void*)page, 0x1000, PROT_READ) != 0) {
        LOGE("vt: restore ro failed errno=%d", errno);
    }
    LOGI("vt: obj=%p vt=%p vt3=%p -> patched in place (vteq=%d)",
         obj, vt, (void*)g_orig_vt3, g_opt_vteq);
    return 0;
}

/* ---------------------------------------------------------------- 配置表 */

static void add_ent(const char* name, const char* value) {
    int i;
    size_t nl, vl;
    if (g_nents >= MAX_ENTS) return;
    nl = strlen(name);
    vl = strlen(value);
    if (nl == 0 || nl >= sizeof(g_ents[0].name)) return;
    if (vl >= sizeof(g_ents[0].value)) return;

    for (i = 0; i < g_nents; i++) {
        if (strcmp(g_ents[i].name, name) == 0) {
            memcpy(g_ents[i].value, value, vl + 1);
            return;
        }
    }
    memcpy(g_ents[g_nents].name, name, nl + 1);
    memcpy(g_ents[g_nents].value, value, vl + 1);
    g_nents++;
}

static void load_defaults(void) {
    int i;
    for (i = 0; g_default[i].name; i++)
        add_ent(g_default[i].name, g_default[i].value);
}

/* 运行时开关:从 /data/local/tmp/fgowhook.cfg 读取,便于免重打包定位问题 */
static void load_cfg(void) {
    FILE* f = fopen("/data/local/tmp/fgowhook.cfg", "re");
    char line[128];
    if (!f) return;
    while (fgets(line, sizeof(line), f)) {
        char* eq = strchr(line, '=');
        char* e;
        int v;
        if (!eq) continue;
        *eq = '\0';
        e = line + strlen(line);
        while (e > line && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        v = atoi(eq + 1);
        if (!strcmp(line, "find"))        g_opt_find = v;
        else if (!strcmp(line, "get"))    g_opt_get = v;
        else if (!strcmp(line, "spoof"))  g_opt_spoof = v;
        else if (!strcmp(line, "secure")) g_opt_secure = v;
        else if (!strcmp(line, "trace"))  g_trace = v;
        else if (!strcmp(line, "noop"))   g_opt_noop = v;
        else if (!strcmp(line, "vt"))     g_opt_vt = v;
        else if (!strcmp(line, "poke"))   g_opt_poke = v;
        else if (!strcmp(line, "vteq"))   g_opt_vteq = v;
        else if (!strcmp(line, "areadump")) g_area_dump = v;
        else if (!strcmp(line, "tmpl"))   g_opt_tmpl = v;
        else if (!strcmp(line, "area"))   g_opt_area = v;
        else if (!strcmp(line, "tmplall"))g_opt_tmplall = v;
        LOGI("cfg: %s = %d", line, v);
    }
    fclose(f);
}

static void load_conf_file(const char* path) {
    FILE* f = fopen(path, "re");
    char line[320];
    int n = 0;
    if (!f) return;
    LOGI("reading spoof list from %s", path);
    while (fgets(line, sizeof(line), f)) {
        char* eq;
        char* p = line;
        char* v;
        char* e;
        char* ns;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = '\0';
        v = eq + 1;
        e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
        ns = p + strlen(p);
        while (ns > p && (ns[-1] == ' ' || ns[-1] == '\t')) *--ns = '\0';
        if (*p && *v) { add_ent(p, v); n++; }
    }
    fclose(f);
    LOGI("loaded %d entries from %s", n, path);
}

/* ---------------------------------------------------------------- 入口 */

__attribute__((constructor))
static void fgow_hook_init(void) {
    void* find_thunk = dlsym(RTLD_DEFAULT, "__system_property_find");
    void* get_thunk  = dlsym(RTLD_DEFAULT, "__system_property_get");
    int i;
    int ok = 0, miss = 0;

    if (!find_thunk || !get_thunk) {
        LOGE("libc property thunks not found (find=%p get=%p)", find_thunk, get_thunk);
        return;
    }

    /* 从 thunk 机器码里解出 SystemProperties 对象地址与真实现地址 */
    for (i = 0; i < 6; i++) {
        void* p = (uint8_t*)find_thunk + i * 4;
        if (!g_sysprops) { void* a = decode_adr(p); if (a) g_sysprops = a; }
        if (!g_real_find) { void* b = decode_b(p);  if (b) g_real_find = (find_impl_t)b; }
    }
    for (i = 0; i < 8; i++) {
        void* p = (uint8_t*)get_thunk + i * 4;
        if (!g_real_get) { void* b = decode_b(p); if (b) g_real_get = (get_impl_t)b; }
    }

    if (!g_sysprops || !g_real_find || !g_real_get) {
        LOGE("decode failed: sysprops=%p find=%p get=%p",
             g_sysprops, (void*)g_real_find, (void*)g_real_get);
        return;
    }
    LOGI("decoded: sysprops=%p find_impl=%p get_impl=%p",
         g_sysprops, (void*)g_real_find, (void*)g_real_get);
    {
        uint32_t co = scan_ctx_off((const void*)g_real_find);
        if (co) g_ctx_off = co;
        LOGI("ctx_off = 0x%x (builtin default 0x%x)", g_ctx_off, OFF_FINDER);
    }

    load_cfg();
    load_defaults();
    for (i = 0; g_conf_paths[i]; i++) { load_conf_file(g_conf_paths[i]); }

    /* 为每一项构造伪造 prop_info */
    for (i = 0; i < g_nents; i++) {
        int rc;
        if (strcmp(g_ents[i].name, "ro.secure") == 0) {
            /* 真实环境中 ro.secure 存在但 app 域读不到,用同前缀、同值长度
             * 的 ro.adb.secure 克隆后改名,serial 编码天然正确。 */
            rc = build_fake(i, "ro.adb.secure", "ro.secure", g_ents[i].value);
            if (rc != 0) rc = build_fake(i, "ro.debuggable", "ro.secure", g_ents[i].value);
            if (rc == 0) LOGI("fake ro.secure built (template ro.adb.secure)");
            else         LOGE("cannot build fake ro.secure");
        } else {
            /* 其余项沿用原库语义:只在属性真实存在时才伪造 */
            prop_info_t tmpl = g_real_find(g_sysprops, g_ents[i].name);
            if (!tmpl) { miss++; continue; }
            rc = build_fake(i, g_ents[i].name, g_ents[i].name, g_ents[i].value);
        }
        if (rc == 0) ok++;
    }
    LOGI("fake prop_info: %d built, %d skipped (not present)", ok, miss);

    if (g_opt_area) {
        int na = 0;
        for (i = 0; i < g_nents; i++) {
            if (build_area(i, g_ents[i].name, g_ents[i].value) == 0) na++;
        }
        LOGI("mini areas built: %d/%d", na, g_nents);
    }

    int e1 = -99, e2 = -99;
    if (g_opt_poke) {
        /* 只往 libc 的 bss 写一个字节(远离关键字段),验证检测范围 */
        volatile unsigned char* p = (volatile unsigned char*)g_sysprops + 0x600;
        unsigned char old = *p;
        *p = 0x5A;
        LOGI("poke: wrote 0x5A over 0x%02x at %p (within libc bss)", old, (void*)p);
    }
    if (g_opt_vt || g_opt_tmpl || g_opt_tmplall || g_opt_area) {
        int rv = install_vthijack();
        LOGI("vt hijack -> %d", rv);
    }
    if (g_opt_find) {
        e1 = patch_thunk(find_thunk, (void*)hook_find);
        LOGI("patch __system_property_find @%p -> %d", find_thunk, e1);
    } else {
        LOGI("hook_find DISABLED");
    }
    if (g_opt_get) {
        e2 = patch_thunk(get_thunk, (void*)hook_get);
        LOGI("patch __system_property_get  @%p -> %d", get_thunk, e2);
    } else {
        LOGI("hook_get DISABLED");
    }

    /* 自检:走一遍被 hook 的公开入口 */
    char buf[128];
    int r = __system_property_get("ro.secure", buf);
    LOGI("selftest: ro.secure -> ret=%d value=\"%s\"", r, buf);
    r = __system_property_get("ro.build.version.release", buf);
    LOGI("selftest: ro.build.version.release -> ret=%d value=\"%s\"", r, buf);
    r = __system_property_get("ro.build.version.sdk", buf);
    LOGI("selftest: ro.build.version.sdk -> ret=%d value=\"%s\"", r, buf);
    if (g_opt_area && g_trace) {
        int i;
        for (i = 0; i < g_nents; i++) {
            char vb[128];
            int rr = __system_property_get(g_ents[i].name, vb);
            LOGI("  chk %-45s want='%s' got(%d)='%s'%s",
                 g_ents[i].name, g_ents[i].value, rr, vb,
                 (rr > 0 && !strcmp(vb, g_ents[i].value)) ? "" : "   <== MISMATCH");
        }
    }
    LOGI("FGO waltz hook installed (pid %d)", getpid());
}
