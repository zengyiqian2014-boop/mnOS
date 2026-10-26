/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "commands.h"
#include <string.h>
struct manual {
    const char *name, *usage, *en, *cn, *detail_en, *detail_cn, *example;
};
/* This table is also the completion registry. Entries describe real handlers. */
static const struct manual guide[] = {
{"linux", "[kernel] linux start|status|pause|resume|stop|restart|send COMMAND", "Manage the software Linux guest", "管理软件模拟的 Linux 客体",
 "No root: start opens a Linux console in the native GUI. Original C\nRV64IMAC/Sv39 interpretation runs an actual Linux kernel and BusyBox.\n64 MiB guest RAM, one emulated CPU. pause/resume preserve guest state;\nstop/restart discard ALL guest files. Host account changes wipe RAM.\nGuest root cannot access host files, disks, devices or network.\nOnly compatible RV64IMAC/LP64 programs run; no guest GPU, floating-point\nISA or desktop apps. Use Linux Edition for Firefox, VS Code and full CTF.\nIn the guest: busybox --list and COMMAND --help. F5 returns to host.\nsend queues a Linux command; it does not execute a native host command.",
 "无需 root：start 在自研桌面打开 Linux 控制台。自研 C 模拟器\n执行 RV64IMAC/Sv39 指令，运行真实 Linux 内核及 BusyBox。\n客体使用 64 MiB RAM、一个软件 CPU；pause/resume 保留状态，\nstop/restart 会丢弃全部客体文件。宿主切换账号时擦除客体 RAM。\n客体 root 不能访问宿主文件、磁盘、设备和网络。\n仅运行兼容 RV64IMAC/LP64 的程序，未实现客体 GPU、浮点指令或桌面应用。\nFirefox、VS Code 和完整 CTF 桌面使用 Linux Edition。\n客体内 busybox --list 列命令、命令 --help 查帮助；F5 返回宿主。\nsend 发送客体命令，不作为宿主命令执行。", "linux start"},
{"system", "system [status|cpu|memory|processes|storage|security|display|guest]", "Unified native system management", "统一原生系统管理入口",
 "Read-only status queries use native services; processes lists native\napplications, storage reports MNFS, guest reports the emulated Linux\nmachine. Use Settings for personal display and wallpaper, ps/kill for\nowned processes, passwd for your password. su authenticates account\nand volume administration. Linux services/packages are managed inside\nthe Linux environment, not by pretending native drivers exist.",
 "只读状态调用真实原生服务：processes 列原生进程，storage 查 MNFS，\nguest 查 Linux 模拟机。设置窗口管理个人显示与壁纸；ps/kill 管理\n自己的进程；passwd 改自己的密码。账号和卷管理需要 su 验证。\nLinux 的服务和软件包由 Linux 环境管理，不冒充原生驱动能力。", "system status"},
{"help", "help [-cn] [COMMAND|all]", "Read the command manual", "阅读命令手册",
 "Default language: English. -cn selects Chinese. Use all for every entry.\nUse Up/Down to scroll output; Tab completes command names.\nExamples use USER as a placeholder for your own account name.",
 "默认使用英文；-cn 指定中文；all 展开全部条目。\n上下方向键翻阅输出，Tab 补全命令。\n示例中的 USER 要替换为你自己的账号名。", "help -cn kernel display"},
{"kernel", "kernel INSTRUCTION [OBJECT]", "Control the native kernel", "控制原生内核",
 "Calls actual mnOS services. Queries and personal display settings need\nno root. Volume administration requires su authentication.\nFile/process/account commands below also accept the kernel prefix.\nThis kernel has no Linux ABI, loadable driver modules or network stack.",
 "调用实际的 mnOS 服务。查询和个人显示设置无需 root。\n卷管理操作需要先用 su 验证管理员身份。\n下列文件、进程和账号命令也可以加 kernel 前缀。\n当前没有 Linux ABI、动态驱动模块或网络协议栈。", "kernel info"},
{"info", "kernel info", "Kernel and architecture", "查看内核及架构",
 "Read-only: reports the original C/assembly kernel, CPU name and online\nCPU count. uname is an alias. Does not claim Linux compatibility.",
 "只读：显示自研 C/汇编内核、CPU 名称和在线核心数。\nuname 是此命令的别名，不代表兼容 Linux 应用。", "kernel info"},
{"cpu", "kernel cpu", "Processor discovery", "处理器识别状态",
 "Read-only: firmware-discovered and successfully started CPU counts.\nA discovered CPU is not necessarily online. Hotplug is not implemented.",
 "只读：显示固件发现的核心数和成功启动的核心数。\n发现核心不代表核心已经在线；尚未实现热插拔。", "kernel cpu"},
{"memory", "kernel memory", "Physical memory accounting", "物理内存统计",
 "Read-only: free/usable 4096-byte pages and physical address limit.\nMultiply page counts by 4096 for bytes. The address limit is not RAM\ncapacity. mem is an alias; no memory is allocated by this query.",
 "只读：空闲与可用的 4096 字节页数，以及物理地址上限。\n页数乘以 4096 为字节数。地址上限不等于实际内存容量。\nmem 是别名；查询本身不会分配物理页。", "kernel memory"},
{"uptime", "kernel uptime", "Time since this boot", "本次启动的运行时间",
 "Read-only: seconds from the kernel timer. This is uptime, not wall-clock\ntime or a measurement of performance.",
 "只读：根据内核定时器计算启动后的秒数。\n这是运行时长，不是日历时间，也不是性能测试。", "uptime"},
{"tasks", "kernel tasks", "Kernel worker scheduling", "内核工作任务调度",
 "Read-only: context-switch counters for the boot CPU test workers.\nUse ps for native user applications. These are different schedulers.",
 "只读：启动核心测试工作任务的上下文切换计数。\n原生用户应用使用 ps 查看；两者不是同一组任务。", "kernel tasks"},
{"gpu", "kernel gpu", "Active display driver", "当前显示驱动",
 "Read-only: names the active native framebuffer/display driver.\nA framebuffer does not imply GPU acceleration or arbitrary mode changes.",
 "只读：查看当前帧缓冲或显示驱动。\n帧缓冲可显示画面，并不代表已支持 GPU 加速或任意分辨率。", "kernel gpu"},
{"devices", "kernel devices", "Enumerated PCI devices", "已枚举的 PCI 设备",
 "Read-only PCI enumeration. A listed device can have no native driver.\nUse Up/Down for long output. pci is an alias.",
 "只读 PCI 枚举。列出设备不代表已有原生驱动。\n输出较长时用上下键翻阅；pci 是别名。", "kernel devices"},
{"test", "kernel test", "Boot isolation results", "启动隔离测试结果",
 "Read-only: shows completed boot isolation checks. It does not re-run\ntests or certify system security. Individual results are in serial logs.",
 "只读：显示本次启动已经完成的隔离检查，不会重新执行测试。\n逐项结果在串口日志中；通过检查不等于安全认证。", "kernel test"},
{"security", "kernel security", "Session and protection status", "会话与保护状态",
 "Read-only: current UID, root status and architecture protection state.\nNative pages use W^X and user/kernel isolation. Disks are not encrypted;\nthe desktop is kernel-resident. This report is not a security audit.",
 "只读：当前 UID、管理员状态和架构保护信息。\n原生页具备 W^X 与用户隔离；磁盘尚未加密，桌面在内核运行。\n这些状态信息不等于完整安全审计。", "kernel security"},
{"display", "kernel display [modes|WIDTHxHEIGHT]", "Query or change resolution", "查询或调整分辨率",
 "No root: without arguments shows the current mode; modes lists candidate\nsizes. Applying a mode succeeds only if the driver and VRAM support it.\nA firmware-only framebuffer keeps its boot mode. Save with save-settings.",
 "无需 root：不带参数显示当前模式；modes 列出候选分辨率。\n仅在驱动及显存支持时才会应用；固件帧缓冲保留启动分辨率。\n用 save-settings 保存，下次登录时自动恢复。", "kernel display 1600x900"},
{"wallpaper", "kernel wallpaper [0|1|2]", "Query or change wallpaper", "查询或切换壁纸",
 "No root: 0 contours, 1 grid, 2 solid. Omit the object to query the ID.\nChanges affect this session; save-settings persists your own preference.\nCustom image-file wallpapers are not implemented.",
 "无需 root：0 流线、1 网格、2 纯色；省略参数查看当前编号。\n先改变本次会话，用 save-settings 保存个人偏好。\n尚未支持把任意图片文件设为壁纸。", "kernel wallpaper 1"},
{"save-settings", "kernel save-settings", "Save personal display preferences", "保存个人显示设置",
 "No root for your own home. Writes display.cfg on /disk. The disk must be\nmounted and writable. RAM-only sessions cannot save persistent settings.\nA root-imposed disk lock blocks writes until root unlocks it.",
 "个人设置无需 root。写入 /disk 上个人目录的 display.cfg。\n磁盘必须已挂载且可写；只有内存卷时无法持久保存。\nroot 锁定磁盘后，需要 root 解锁才可写入。", "kernel save-settings"},
{"fonts", "kernel fonts", "Font library status", "字库状态",
 "Read-only: counts supported Unicode scalars and raster metrics.\nASCII printable characters, CJK, Latin, Kana, Hangul and many symbols\nare embedded. Missing glyphs use U+FFFD. No IME, bidi or shaping yet.",
 "只读：显示支持的 Unicode 码位及字形度量数量。\n内置可打印 ASCII、中日韩文字、拉丁字母和大量符号。\n缺字使用 U+FFFD；尚无输入法、双向排版或复杂文字塑形。", "kernel fonts"},
{"unicode", "kernel unicode", "Render multilingual samples", "显示多语言样例",
 "Read-only visual check for ASCII and two/three/four-byte UTF-8.\nThe editor can render UTF-8 files; keyboard entry is currently ASCII.\nThis displays actual glyphs; it does not install an input method.",
 "只读显示 ASCII 与二、三、四字节 UTF-8 样例。\n编辑器可以显示 UTF-8 文件；当前键盘文字输入仍是 ASCII。\n该命令用于检查实际字形，不会安装输入法。", "kernel unicode"},
{"fs", "[kernel] fs", "Mounted filesystem status", "已挂载文件系统状态",
 "Read-only: RAM/disk presence, free 4096-byte blocks and writable state.\n/ram is temporary. /disk persists on an existing installed MNFS volume.\nThis command never formats, repairs or mounts arbitrary disks.",
 "只读：查看内存卷和磁盘卷、空闲 4096 字节块及可写状态。\n/ram 重启清空；已安装的 MNFS 分区挂载在 /disk。\n此命令不会格式化、修复或挂载任意磁盘。", "kernel fs"},
{"ls", "[kernel] ls [PATH]", "List a directory", "列出目录",
 "Signed-in users can list permitted paths. Omitted PATH selects /ram.\nEntries report file/directory and byte size. Absolute volume prefixes\nselect /ram or /disk. No shell globs or wildcard expansion.",
 "登录后列出有权访问的目录；省略路径时使用 /ram。\n输出目录或文件类型与字节数。路径前缀选择 /ram 或 /disk。\n当前不支持通配符展开。", "ls /disk/home/USER"},
{"stat", "[kernel] stat PATH", "Inspect a file", "查看文件信息",
 "Read-only subject to path permissions. Reports byte size and inode.\nMissing paths, denied access and unavailable volumes return errors.",
 "只读，遵循路径权限；显示字节数和 inode。\n路径不存在、权限不足或未挂载对应卷时会报错。", "stat /disk/home/USER/note.txt"},
{"cat", "[kernel] cat PATH [OFFSET]", "Read a file segment", "读取文件片段",
 "Read-only subject to path permissions. Reads at most 512 bytes starting\nat decimal byte OFFSET (default 0). Empty output reports end-of-file.\nControl/non-ASCII bytes in this diagnostic reader are shown as dots;\nopen the file in Text editor to render its UTF-8 content.",
 "只读，遵循路径权限；从十进制字节偏移处读取最多 512 字节。\n偏移默认 0，空内容显示文件结束。此诊断读取器把控制字符\n和非 ASCII 字节显示为点；UTF-8 文字请用文本编辑器打开。", "cat /disk/home/USER/note.txt 512"},
{"write", "[kernel] write PATH TEXT", "Replace a text file", "替换文本文件内容",
 "Own-home writes need no root. Creates a missing file, writes TEXT and\ntruncates old contents to the new length. Parent directory must exist.\nTEXT is the rest of the line; quotes are literal, not shell syntax.\nErrors include read-only disk, no space and permission denied.",
 "写入个人目录无需 root。文件不存在时创建，写入后截断旧尾部。\n父目录必须存在；TEXT 为本行剩余内容，引号按普通字符保存。\n只读磁盘、空间不足、权限不足等均会返回错误。", "write /disk/home/USER/note.txt Hello world"},
{"writeat", "[kernel] writeat PATH OFFSET TEXT", "Write at a byte offset", "按字节偏移写入",
 "Own-home writes need no root. Decimal OFFSET; keeps bytes after the\nwritten region. Sparse gaps read as zero. Checks 64-bit offset overflow.\nCreates a missing file if its parent exists. No shell quoting.",
 "个人目录无需 root。OFFSET 为十进制字节偏移，保留写入范围后的\n原有内容；稀疏空洞读取为零，检测 64 位偏移溢出。\n父目录存在时可创建新文件；不解释 shell 引号。", "writeat /disk/home/USER/note.txt 6 mnOS"},
{"mkdir", "[kernel] mkdir PATH", "Create one directory", "创建一个目录",
 "Own-home writes need no root. Creates one directory; parent must exist.\nReturns an error for existing paths. There is no recursive -p flag.",
 "在个人目录创建子目录无需 root，父目录必须已存在。\n路径已存在时返回错误；尚无递归 -p 参数。", "mkdir /disk/home/USER/project"},
{"rm", "[kernel] rm PATH", "Remove a file or empty directory", "删除文件或空目录",
 "Own-home writes need no root. Deletes one path; nonempty directories\nare rejected. No -r, -f or wildcard recursion. Removal is permanent\nunless recoverable from an existing MNFS snapshot.",
 "删除个人目录中的文件无需 root。一次删除一个路径，拒绝非空\n目录；没有 -r、-f 或通配符递归。删除不可撤销，除非有现成快照。", "rm /disk/home/USER/old.txt"},
{"mv", "[kernel] mv SOURCE DESTINATION", "Rename or move within one volume", "在同一卷内重命名或移动",
 "Checks source and destination permissions. Both paths must refer to the\nsame volume; cross-volume moves are rejected. Parent must exist.\nProtected system paths cannot be reached through dot-path aliases.",
 "同时检查源和目标权限，两者必须位于同一个卷。\n拒绝跨卷移动；目标父目录必须存在。\n不允许通过点路径别名绕过系统目录保护。", "mv /disk/home/USER/a.txt /disk/home/USER/b.txt"},
{"truncate", "[kernel] truncate PATH SIZE", "Set logical file length", "设置文件逻辑长度",
 "SIZE is a decimal byte count. Own-home writes need no root. Shrinking\ndiscards the tail; extending creates a sparse zero-filled gap.\n64-bit sizes do not imply infinite storage or support in FAT.",
 "SIZE 为十进制字节数，个人目录无需 root。缩短会删除尾部，\n扩展形成读取为零的稀疏空洞。64 位大小不代表无限存储，\n也不代表 FAT 文件系统具备同样能力。", "truncate /disk/home/USER/data.bin 4096"},
{"disk", "kernel disk [status|lock|unlock]", "Control persistent-volume writes", "控制持久卷写入",
 "status (also no object) is read-only and available to normal users.\nlock/unlock require root via su. Lock affects all writers, not just\nthe terminal. Unlock never formats a disk or bypasses file ownership.",
 "status 或省略参数为只读查询，普通用户可用。\nlock/unlock 需要先 su 认证 root。锁定影响所有写入路径。\n解锁不会格式化磁盘，也不会绕过文件归属权限。", "kernel disk status"},
{"snapshot", "[kernel] snapshot save|restore|drop [/ram|/disk]", "Manage the single MNFS snapshot", "管理 MNFS 单个快照",
 "Root required; omitted volume selects /ram. save replaces the saved\nsnapshot; restore replaces current contents with saved contents; drop\nremoves the recovery point. Restore of /disk locks the session because\naccounts are restored too. Back up data before restoring.",
 "需要 root，省略卷名时选择 /ram。save 替换现有快照，restore\n将当前卷恢复为快照状态，drop 删除恢复点。\n恢复 /disk 也会恢复账号记录并锁定会话，需要重新登录。\n恢复前请备份之后新增的数据。", "kernel snapshot save /disk"},
{"gc", "[kernel] gc [/ram|/disk]", "Collect unreferenced MNFS blocks", "回收 MNFS 无引用块",
 "Root required. Omitted volume selects /ram. Reclaims blocks no longer\nreferenced by live metadata or the saved snapshot. Needs writable volume.\nThis is collection, not corruption repair or disk defragmentation.",
 "需要 root。省略卷名时为 /ram，卷必须可写。\n回收不再被当前元数据或已保存快照引用的块。\n这是空间回收，不是损坏修复或磁盘碎片整理。", "kernel gc /disk"},
{"exec", "[kernel] exec PATH", "Launch a native ELF application", "启动原生 ELF 应用",
 "No root: executable must be readable through your path policy. Only\nstatic mnOS ELF64 for the current architecture is accepted. Current\nlimits: one 4 KiB RX segment plus one 4 KiB RW/NX segment; no argv,\nshared libraries or Linux syscalls. Invalid executables are rejected.\nUse ps and kill PID to inspect/stop your applications.",
 "无需 root，但必须有读取目标文件的权限。\n仅接收当前架构的静态 mnOS ELF64：一个 4 KiB RX 代码段和\n一个 4 KiB RW/NX 数据段，尚无参数、共享库和 Linux 系统调用。\n不合要求的文件会被拒绝；用 ps、kill PID 查询或停止自己的程序。", "exec /ram/bin/hello"},
{"ps", "[kernel] ps", "List native processes", "列出原生进程",
 "Normal users see their own processes; root sees all. Reports PID, UID,\nrun ticks, context switches, syscall count, state and executable path.\nThe built-in graphical apps are kernel services, not these user processes.",
 "普通用户查看自己的进程，root 查看全部。\n输出 PID、UID、运行 tick、切换次数、系统调用数、状态和路径。\n内置图形应用属于内核服务，不在该用户进程列表中。", "kernel ps"},
{"kill", "[kernel] kill PID", "Stop a native user process", "停止原生用户进程",
 "PID is a decimal process ID from ps. Users may stop their own process;\nroot may stop any native user process. Invalid/missing IDs are errors.\nNo signal-number options yet. Kernel services cannot be killed here.",
 "PID 为 ps 显示的十进制进程号。普通用户可停止自己的进程，\nroot 可停止所有原生用户进程；无效或不存在的编号会报错。\n尚无信号编号选项，也不能用它终止内核服务。", "kernel kill 2"},
{"whoami", "[kernel] whoami", "Current account name", "当前账号名",
 "Read-only query. Displays the authenticated session name.\nUse kernel security to also see the numeric UID and root status.",
 "只读查询，显示当前已认证会话的账号名。\n用 kernel security 同时查看数字 UID 和管理员状态。", "whoami"},
{"su", "[kernel] su", "Authenticate as root", "认证并切换至 root",
 "Requires the separate root password, not the ordinary user's password.\nInput is masked and omitted from command history/serial output.\nFailed attempts are delayed; root sessions expire after five minutes.\nUse exit to return to the previous ordinary account.",
 "输入独立的 root 密码，不能用普通用户密码代替。\n密码输入被遮挡，不进入命令历史或串口输出。\n失败后有重试延迟，root 会话五分钟后到期；exit 返回原账号。", "su"},
{"exit", "[kernel] exit", "Leave an elevated session", "退出管理员会话",
 "When root was entered through su, returns to the original ordinary\naccount. Otherwise reports that no elevated session can be left.\nThis does not shut down the computer or delete a user account.",
 "通过 su 进入 root 后返回之前的普通账号。\n没有可退出的提权会话时显示提示；不会关机或删除账号。", "exit"},
{"passwd", "[kernel] passwd [NAME]", "Change an account password", "修改账号密码",
 "Ordinary users change only their own password after current-password\nverification. Root can select an existing account. New passwords are\nentered twice, never as command arguments. A damaged/read-only store\nor unavailable secure entropy causes an error, not silent success.",
 "普通用户验证当前密码后只能修改自己的密码，root 可指定现有账号。\n新密码通过交互输入两次，不能放在命令参数中。\n凭据存储损坏、只读或缺少安全随机源时会报错，不会假装保存成功。", "passwd"},
{"useradd", "[kernel] useradd NAME", "Create a local account", "创建本地账号",
 "Root required. Prompts twice for the new password, then persists the\naccount. Existing/reserved names are rejected. Native account storage\ncurrently has 16 slots including root; full storage is reported.",
 "需要 root。交互输入两次新密码后持久保存账号。\n拒绝已存在或系统保留的名字。当前原生存储含 root 最多 16 个\n账号，容量用完会明确报错。", "useradd developer"},
{"lock", "[kernel] lock", "Lock the current session", "锁定当前会话",
 "No root required. Locks desktop and terminal immediately. Further input\ngoes to account authentication. logout is an alias. Unsaved documents\nremain in memory but are not a persistent backup.",
 "无需 root，立即锁定桌面和终端，后续输入进入账号认证。\nlogout 为别名。未保存文档仍在内存中，但不属于持久备份。", "lock"},
{"clear", "[kernel] clear", "Clear terminal output", "清空终端输出",
 "No root required. Clears only visible output/scrollback; does not erase\nfiles or the current boot's serial diagnostic log.",
 "无需 root。只清除终端输出和翻阅记录，不删除文件，\n也不会擦除本次启动的串口诊断日志。", "clear"},
{"reboot", "[kernel] reboot", "Restart this machine", "重新启动机器",
 "Available to this signed-in local-console user. Opens the power\nconfirmation: R reboot, S power off, Escape cancel. Save documents first.\nRAM files and unsaved preferences disappear after restarting.",
 "本地已登录用户可用，打开电源确认：R 重启，S 关机，Esc 取消。\n请先保存文档；重启会丢失内存卷文件及未保存的偏好。", "kernel reboot"},
{"shutdown", "[kernel] shutdown", "Power confirmation", "关机确认",
 "Available to the signed-in local-console user. Opens the confirmation\ndialog; S powers off, R restarts, Escape cancels. Firmware support is\nrequired for power-off. No automatic shutdown timer is implemented.",
 "本地已登录用户可用，打开电源确认：S 关机，R 重启，Esc 取消。\n关机依赖固件支持，尚未实现自动关机定时器。", "kernel shutdown"},
{"tools", "tools [native|linux]", "Application environment and availability", "工具环境与可用状态",
 "native lists built-in tools and the static ELF environment. linux\nexplains Firefox/VS Code and APT in the Linux tools environment of the\ncombined installer. Those programs do not run on this native kernel.\nNo network/package manager is implemented in the native environment.\nUse the Linux tools environment for apt, compilers and Linux apps.",
 "native 列出内置工具和静态 ELF 环境；linux 说明集成安装包的\nLinux 工具环境中 Firefox、VS Code 和 APT 的用法。\n这些程序目前不能运行在自研内核上；原生环境暂无网络和包管理器。\n安装第三方 Linux 软件、使用完整编译器，需要进入 Linux 工具环境。", "tools linux"}
};
static void lines(const char *s, command_output output) {
    char line[256];
    while (*s) {
        size_t n=0;
        while (*s && *s!='\n' && n<sizeof line-1) line[n++]=*s++;
        line[n]=0; output(line); if (*s=='\n') s++;
    }
}
unsigned command_count(void) { return sizeof guide/sizeof *guide; }
const char *command_name(unsigned i) { return i<command_count()?guide[i].name:NULL; }
static const char *alias(const char *s) {
    if (!strcmp(s,"uname")) return "info";
    if (!strcmp(s,"mem")) return "memory";
    if (!strcmp(s,"pci")) return "devices";
    if (!strcmp(s,"logout")) return "lock";
    return s;
}
void command_help(const char *args, command_output output) {
    bool cn=false; const char *topic=args;
    while (*topic==' ')topic++;
    if (!strncmp(topic,"-cn",3) && (!topic[3] || topic[3]==' ')) {cn=true;topic+=3;while(*topic==' ')topic++;}
    if (!strncmp(topic,"kernel ",7)) topic+=7;
    topic=alias(topic);
    if (!*topic) {
        output(cn?"mnOS 中文帮助：help -cn 命令；help -cn all 查看全部":"mnOS command manual: help COMMAND; help all for all entries");
        output(cn?"上下键翻阅；Tab 补全。个人操作无需 root，卷管理需要 root。":"Up/Down scroll; Tab completes. Own-home work needs no root.");
        for(unsigned i=0;i<command_count();i++) {
            output(guide[i].usage); output(cn?guide[i].cn:guide[i].en);
        }
        output(cn?"/ram 临时；/disk 持久。F7 打开显示设置。":"/ram is temporary; /disk is persistent. F7 opens display settings.");
        return;
    }
    bool all=!strcmp(topic,"all"),found=false;
    for(unsigned i=0;i<command_count();i++) if(all || !strcmp(topic,guide[i].name)) {
        found=true; output(guide[i].usage); output(cn?guide[i].cn:guide[i].en);
        lines(cn?guide[i].detail_cn:guide[i].detail_en,output);
        output(cn?"示例：":"Example:"); output(guide[i].example); output("");
    }
    if(!found) output(cn?"没有此命令的帮助。输入 help -cn 查看命令目录。":"Unknown help topic. Use help for the command index.");
}
bool command_complete(char *line, size_t cap, command_output output) {
    size_t length=strlen(line),start=0;
    if (!cap || length>=cap) return false;
    if(!strncmp(line,"kernel ",7)) start=7;
    if(!strncmp(line+start,"help ",5)) {start+=5;if(!strncmp(line+start,"-cn ",4))start+=4;if(!strncmp(line+start,"kernel ",7))start+=7;}
    const char *choices[64]; unsigned n=0;
    const char *prefix=line+start;
    if(!strncmp(prefix,"display ",8)){ start+=8;choices[n++]="modes";choices[n++]="1024x768";choices[n++]="1280x720";choices[n++]="1600x900";choices[n++]="1920x1080";choices[n++]="2560x1440"; }
    else if(!strncmp(prefix,"disk ",5)){start+=5;choices[n++]="status";choices[n++]="lock";choices[n++]="unlock";}
    else if(!strncmp(prefix,"snapshot ",9)){start+=9;choices[n++]="save";choices[n++]="restore";choices[n++]="drop";}
    else if(!strncmp(prefix,"wallpaper ",10)){start+=10;choices[n++]="0";choices[n++]="1";choices[n++]="2";}
    else if(!strncmp(prefix,"tools ",6)){start+=6;choices[n++]="native";choices[n++]="linux";}
    else if(!strncmp(prefix,"linux ",6)){start+=6;choices[n++]="start";choices[n++]="status";choices[n++]="pause";choices[n++]="resume";choices[n++]="stop";choices[n++]="restart";choices[n++]="send";}
    else if(!strncmp(prefix,"system ",7)){start+=7;choices[n++]="status";choices[n++]="cpu";choices[n++]="memory";choices[n++]="processes";choices[n++]="storage";choices[n++]="security";choices[n++]="display";choices[n++]="guest";}
    else { for(unsigned i=0;i<command_count();i++) choices[n++]=guide[i].name; choices[n++]="uname";choices[n++]="mem";choices[n++]="pci";choices[n++]="logout"; }
    size_t typed=length-start,common=0;const char *first=NULL;unsigned matches=0;
    for(unsigned i=0;i<n;i++)if(!strncmp(choices[i],line+start,typed)) {
        if(!matches++){first=choices[i];common=strlen(first);}else{size_t j=0;while(j<common && first[j]==choices[i][j])j++;common=j;}
    }
    if(!matches) return false;
    if(common>typed && start+common<cap) {memcpy(line+start,first,common);line[start+common]=0;}
    if(matches==1) {size_t used=strlen(line);if(used+1<cap){line[used]=' ';line[used+1]=0;}}
    else for(unsigned i=0;i<n;i++)if(!strncmp(choices[i],line+start,typed))output(choices[i]);
    return true;
}
