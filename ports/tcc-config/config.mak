# rbOS tcc configuration
TOPSRC = .
TOP = .

CC = ../rbos-cc
AR = ar
RANLIB = ranlib
CFLAGS = -Wall -O2 -g -D_DEFAULT_SOURCE
LDFLAGS = -dynamic

CONFIG_static = yes
CONFIG_os = Linux
CONFIG_x86_64 = yes
CONFIG_single_file = no
CONFIG_tcc_backtrace = no
# HEAD lib/Makefile tests $(CONFIG_backtrace) (name changed upstream);
# must be present and != yes, or backtrace objects get built.
CONFIG_backtrace = no
CONFIG_bcheck = no
CONFIG_selinux = no
CONFIG_pie = no
CONFIG_pic = no
CONFIG_pthread = no
CONFIG_ldl = no
CONFIG_semlock = no
CONFIG_new_dtags = no
CONFIG_codesign = no
CONFIG_tcc_predefs = no

LIBSUF = .a
EXESUF =
DLLSUF = .so

VERSION = 0.9.28rc
PREFIX = /usr

# make's (config.mak missing these, stock ./configure provides them)
ARCH = x86_64
CC_NAME = gcc
GCC_MAJOR = 14
GCC_MINOR = 2

bindir = $(PREFIX)/bin
libdir = $(PREFIX)/lib
tccdir = $(PREFIX)/lib/tcc
includedir = $(PREFIX)/include
mandir = $(PREFIX)/share/man
infodir = $(PREFIX)/share/info
docdir = $(PREFIX)/share/doc
