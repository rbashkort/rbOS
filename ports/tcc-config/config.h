/* Automatically generated config.h for rbOS tcc */
#ifndef CONFIG_H
#define CONFIG_H

#define TCC_VERSION "0.9.28rc"
#define TCC_VERSION_NUM 928

/* Target */
#define TCC_TARGET_X86_64 1

/* OS */
#define TARGETOS_LINUX 1

/* Paths - rbOS specific */
#define CONFIG_TCC_SYSINCLUDEPATHS "/usr/include"
#define CONFIG_TCC_LIBPATHS "/usr/lib"
#define CONFIG_TCC_CRTPREFIX "/usr/lib"
#define CONFIG_TCC_ELFINTERP ""
#define CONFIG_TCC_SWITCHES ""
#define CONFIG_TRIPLET "x86_64-pc-linux-gnu"
#define CONFIG_OS_RELEASE ""
#define CONFIG_SYSROOT ""
#define CONFIG_TCCDIR "/usr/lib/tcc"
#define CONFIG_TCC_PREDEFS 0

/* Features disabled for rbOS */
#define CONFIG_SELINUX 0
#define CONFIG_TCC_PIE 0
#define CONFIG_TCC_PIC 0
#define CONFIG_TCC_BCHECK 0
#define CONFIG_TCC_BACKTRACE 0
#define CONFIG_TCC_SEMLOCK 0
#define CONFIG_NEW_DTAGS 0
#define CONFIG_CODESIGN 0

#endif /* CONFIG_H */
