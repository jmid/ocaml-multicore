/**************************************************************************/
/*                                                                        */
/*                                 OCaml                                  */
/*                                                                        */
/*             Xavier Leroy, projet Cristal, INRIA Rocquencourt           */
/*                                                                        */
/*   Copyright 1996 Institut National de Recherche en Informatique et     */
/*     en Automatique.                                                    */
/*                                                                        */
/*   All rights reserved.  This file is distributed under the terms of    */
/*   the GNU Lesser General Public License version 2.1, with the          */
/*   special exception on linking described in the file LICENSE.          */
/*                                                                        */
/**************************************************************************/

#define CAML_INTERNALS

/* Basic system calls */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h> /* for _wchdir and _wgetcwd */
#else
#include <sys/wait.h>
#endif
#include "caml/config.h"
#ifdef HAS_UNISTD
#include <unistd.h>
#endif
#ifdef HAS_TIMES
#include <sys/times.h>
#endif
#ifdef HAS_GETRUSAGE
#include <sys/time.h>
#include <sys/resource.h>
#endif
#ifdef HAS_GETTIMEOFDAY
#include <sys/time.h>
#endif
#include "caml/alloc.h"
#include "caml/debugger.h"
#include "caml/fail.h"
#include "caml/gc_ctrl.h"
#include "caml/major_gc.h"
#include "caml/io.h"
#include "caml/mlvalues.h"
#include "caml/osdeps.h"
#include "caml/signals.h"
#include "caml/fiber.h"
#include "caml/sys.h"
#include "caml/startup.h"
#include "caml/callback.h"
#include "caml/startup_aux.h"
#include "caml/major_gc.h"
#include "caml/shared_heap.h"

static char * error_message(void)
{
  return strerror(errno);
}

#ifndef EAGAIN
#define EAGAIN (-1)
#endif
#ifndef EWOULDBLOCK
#define EWOULDBLOCK (-1)
#endif

CAMLexport void caml_sys_error(value arg)
{
  CAMLparam1 (arg);
  char * err;
  CAMLlocal1 (str);

  err = error_message();
  if (arg == NO_ARG) {
    str = caml_copy_string(err);
  } else {
    mlsize_t err_len = strlen(err);
    mlsize_t arg_len = caml_string_length(arg);
    str = caml_alloc_string(arg_len + 2 + err_len);
    memmove(&Byte(str, 0), String_val(arg), arg_len);
    memmove(&Byte(str, arg_len), ": ", 2);
    memmove(&Byte(str, arg_len + 2), err, err_len);
  }
  caml_raise_sys_error(str);
  CAMLnoreturn;
}

CAMLexport void caml_sys_io_error(value arg)
{
  if (errno == EAGAIN || errno == EWOULDBLOCK) {
    caml_raise_sys_blocked_io();
  } else {
    caml_sys_error(arg);
  }
}

/* Check that [name] can safely be used as a file path */

static void caml_sys_check_path(value name)
{
  if (! caml_string_is_c_safe(name)) {
    errno = ENOENT;
    caml_sys_error(name);
  }
}

CAMLexport void caml_do_exit(int retcode)
{
  caml_domain_state* domain_state = Caml_state;
  struct gc_stats s;

  if ((caml_params->verb_gc & 0x400) != 0) {
    caml_sample_gc_stats(&s);
    {
      /* cf caml_gc_counters */
      double minwords = s.minor_words
        + (double) (domain_state->young_end - domain_state->young_ptr);
      double majwords = s.major_words + (double) domain_state->allocated_words;
      double allocated_words = minwords + majwords - s.promoted_words;
      intnat heap_words =
        s.major_heap.pool_words + s.major_heap.large_words;
      intnat top_heap_words =
        s.major_heap.pool_max_words + s.major_heap.large_max_words;

      if (heap_words == 0) {
        heap_words = Wsize_bsize(caml_heap_size(Caml_state->shared_heap));
      }

      if (top_heap_words == 0) {
        top_heap_words = caml_top_heap_words(Caml_state->shared_heap);
      }

      caml_gc_message(0x400, "allocated_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat)allocated_words);
      caml_gc_message(0x400, "minor_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat) minwords);
      caml_gc_message(0x400, "promoted_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat) s.promoted_words);
      caml_gc_message(0x400, "major_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat) majwords);
      caml_gc_message(0x400, "minor_collections: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat) s.minor_collections);
      caml_gc_message(0x400, "major_collections: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      domain_state->stat_major_collections);
      caml_gc_message(0x400,
                      "forced_major_collections: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      (intnat)s.forced_major_collections);
      caml_gc_message(0x400, "heap_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      heap_words);
      caml_gc_message(0x400, "top_heap_words: %"ARCH_INTNAT_PRINTF_FORMAT"d\n",
                      top_heap_words);
      caml_gc_message(0x400, "mean_space_overhead: %lf\n", caml_mean_space_overhead());
    }
  }

#ifndef NATIVE_CODE
  caml_debugger(PROGRAM_EXIT, Val_unit);
#endif
  if (caml_params->cleanup_on_exit)
    caml_shutdown();
#ifdef _WIN32
  caml_restore_win32_terminal();
#endif
#ifdef NAKED_POINTERS_CHECKER
  if (retcode == 0 && caml_naked_pointers_detected) {
    fprintf (stderr, "\nOut-of-heap pointers were detected by the runtime.\n"
                     "The process would otherwise have terminated normally.\n");
    retcode = 70; /* EX_SOFTWARE; see sysexits.h */
  }
#endif
  exit(retcode);
}

CAMLprim value caml_sys_exit(value retcode)
{
  caml_do_exit(Int_val(retcode));
}

#ifndef O_BINARY
#define O_BINARY 0
#endif
#ifndef O_TEXT
#define O_TEXT 0
#endif
#ifndef O_NONBLOCK
#ifdef O_NDELAY
#define O_NONBLOCK O_NDELAY
#else
#define O_NONBLOCK 0
#endif
#endif

const static int sys_open_flags[] = {
  O_RDONLY, O_WRONLY, O_APPEND | O_WRONLY, O_CREAT, O_TRUNC, O_EXCL,
  O_BINARY, O_TEXT, O_NONBLOCK
};

CAMLprim value caml_sys_open(value path, value vflags, value vperm)
{
  CAMLparam3(path, vflags, vperm);
  int fd, flags, perm;
  char_os * p;

#if defined(O_CLOEXEC)
  flags = O_CLOEXEC;
#elif defined(_WIN32)
  flags = _O_NOINHERIT;
#else
  flags = 0;
#endif

  caml_sys_check_path(path);
  p = caml_stat_strdup_to_os(String_val(path));
  flags |= caml_convert_flag_list(vflags, sys_open_flags);
  perm = Int_val(vperm);
  /* open on a named FIFO can block (PR#8005) */
  caml_enter_blocking_section();
  fd = open_os(p, flags, perm);
  /* fcntl on a fd can block (PR#5069)*/
#if defined(F_SETFD) && defined(FD_CLOEXEC) && !defined(_WIN32) \
  && !defined(O_CLOEXEC)
  if (fd != -1)
    fcntl(fd, F_SETFD, FD_CLOEXEC);
#endif
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (fd == -1) caml_sys_error(path);
  CAMLreturn(Val_long(fd));
}

CAMLprim value caml_sys_close(value fd_v)
{
  int fd = Int_val(fd_v);
  caml_enter_blocking_section();
  close(fd);
  caml_leave_blocking_section();
  return Val_unit;
}

CAMLprim value caml_sys_file_exists(value name)
{
#ifdef _WIN32
  struct _stati64 st;
#else
  struct stat st;
#endif
  char_os * p;
  int ret;

  if (! caml_string_is_c_safe(name)) return Val_false;
  p = caml_stat_strdup_to_os(String_val(name));
  caml_enter_blocking_section();
  ret = stat_os(p, &st);
  caml_leave_blocking_section();
  caml_stat_free(p);

  return Val_bool(ret == 0);
}

CAMLprim value caml_sys_is_directory(value name)
{
  CAMLparam1(name);
#ifdef _WIN32
  struct _stati64 st;
#else
  struct stat st;
#endif
  char_os * p;
  int ret;

  caml_sys_check_path(name);
  p = caml_stat_strdup_to_os(String_val(name));
  caml_enter_blocking_section();
  ret = stat_os(p, &st);
  caml_leave_blocking_section();
  caml_stat_free(p);

  if (ret == -1) caml_sys_error(name);
#ifdef S_ISDIR
  CAMLreturn(Val_bool(S_ISDIR(st.st_mode)));
#else
  CAMLreturn(Val_bool(st.st_mode & S_IFDIR));
#endif
}

CAMLprim value caml_sys_remove(value name)
{
  CAMLparam1(name);
  char_os * p;
  int ret;
  caml_sys_check_path(name);
  p = caml_stat_strdup_to_os(String_val(name));
  caml_enter_blocking_section();
  ret = unlink_os(p);
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (ret != 0) caml_sys_error(name);
  CAMLreturn(Val_unit);
}

CAMLprim value caml_sys_rename(value oldname, value newname)
{
  char_os * p_old;
  char_os * p_new;
  int ret;
  caml_sys_check_path(oldname);
  caml_sys_check_path(newname);
  p_old = caml_stat_strdup_to_os(String_val(oldname));
  p_new = caml_stat_strdup_to_os(String_val(newname));
  caml_enter_blocking_section();
  ret = rename_os(p_old, p_new);
  caml_leave_blocking_section();
  caml_stat_free(p_new);
  caml_stat_free(p_old);
  if (ret != 0)
    caml_sys_error(NO_ARG);
  return Val_unit;
}

CAMLprim value caml_sys_chdir(value dirname)
{
  CAMLparam1(dirname);
  char_os * p;
  int ret;
  caml_sys_check_path(dirname);
  p = caml_stat_strdup_to_os(String_val(dirname));
  caml_enter_blocking_section();
  ret = chdir_os(p);
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (ret != 0) caml_sys_error(dirname);
  CAMLreturn(Val_unit);
}

CAMLprim value caml_sys_mkdir(value path, value perm)
{
  CAMLparam2(path, perm);
  char_os * p;
  int ret;
  caml_sys_check_path(path);
  p = caml_stat_strdup_to_os(String_val(path));
  caml_enter_blocking_section();
  ret = mkdir_os(p, Int_val(perm));
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (ret == -1) caml_sys_error(path);
  CAMLreturn(Val_unit);
}

CAMLprim value caml_sys_rmdir(value path)
{
  CAMLparam1(path);
  char_os * p;
  int ret;
  caml_sys_check_path(path);
  p = caml_stat_strdup_to_os(String_val(path));
  caml_enter_blocking_section();
  ret = rmdir_os(p);
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (ret == -1) caml_sys_error(path);
  CAMLreturn(Val_unit);
}

CAMLprim value caml_sys_getcwd(value unit)
{
  char_os buff[4096];
  char_os * ret;
#ifdef HAS_GETCWD
  ret = getcwd_os(buff, sizeof(buff)/sizeof(*buff));
#else
  caml_invalid_argument("Sys.getcwd not implemented");
#endif /* HAS_GETCWD */
  if (ret == 0) caml_sys_error(NO_ARG);
  return caml_copy_string_of_os(buff);
}

CAMLprim value caml_sys_unsafe_getenv(value var)
{
  char_os * res, * p;
  value val;

  if (! caml_string_is_c_safe(var)) caml_raise_not_found();
  p = caml_stat_strdup_to_os(String_val(var));
#ifdef _WIN32
  res = caml_win32_getenv(p);
#else
  res = getenv(p);
#endif
  caml_stat_free(p);
  if (res == 0) caml_raise_not_found();
  val = caml_copy_string_of_os(res);
#ifdef _WIN32
  caml_stat_free(res);
#endif
  return val;
}

CAMLprim value caml_sys_getenv(value var)
{
  char_os * res, * p;
  value val;

  if (! caml_string_is_c_safe(var)) caml_raise_not_found();
  p = caml_stat_strdup_to_os(String_val(var));
#ifdef _WIN32
  res = caml_win32_getenv(p);
#else
  res = caml_secure_getenv(p);
#endif
  caml_stat_free(p);
  if (res == 0) caml_raise_not_found();
  val = caml_copy_string_of_os(res);
#ifdef _WIN32
  caml_stat_free(res);
#endif
  return val;
}

static value main_argv;

CAMLprim value caml_sys_get_argv(value unit)
{
  CAMLparam0 ();   /* unit is unused */
  CAMLlocal2 (exe_name, res);
  exe_name = caml_copy_string_of_os(caml_params->exe_name);
  res = caml_alloc_small(2, 0);
  caml_initialize(&Field(res, 0), exe_name);
  caml_initialize(&Field(res, 1), main_argv);
  CAMLreturn(res);
}

CAMLprim value caml_sys_argv(value unit)
{
  return main_argv;
}

CAMLprim value caml_sys_modify_argv(value new_argv)
{
  caml_modify_generational_global_root(&main_argv, new_argv);
  return Val_unit;
}

CAMLprim value caml_sys_executable_name(value unit)
{
  return caml_copy_string_of_os(caml_params->exe_name);
}

void caml_sys_init(char_os * exe_name, char_os **argv)
{
  value v;
#ifdef _WIN32
  /* Initialises the caml_win32_* globals on Windows with the version of
     Windows which is running */
  caml_probe_win32_version();
#if WINDOWS_UNICODE
  caml_setup_win32_terminal();
#endif
#endif
  caml_init_exe_name(exe_name);
  v = caml_alloc_array((void *)caml_copy_string_of_os,
                       (char const **) argv);
  main_argv = v;
  caml_register_generational_global_root(&main_argv);
}

#ifdef _WIN32
#define WIFEXITED(status) 1
#define WEXITSTATUS(status) (status)
#else
#if !(defined(WIFEXITED) && defined(WEXITSTATUS))
/* Assume old-style V7 status word */
#define WIFEXITED(status) (((status) & 0xFF) == 0)
#define WEXITSTATUS(status) (((status) >> 8) & 0xFF)
#endif
#endif

#ifdef HAS_SYSTEM
CAMLprim value caml_sys_system_command(value command)
{
  CAMLparam1 (command);
  int status, retcode;
  char_os *buf;

  if (! caml_string_is_c_safe (command)) {
    errno = EINVAL;
    caml_sys_error(command);
  }
  buf = caml_stat_strdup_to_os(String_val(command));
  caml_enter_blocking_section ();
  status = system_os(buf);
  caml_leave_blocking_section ();
  caml_stat_free(buf);
  if (status == -1) caml_sys_error(command);
  if (WIFEXITED(status))
    retcode = WEXITSTATUS(status);
  else
    retcode = 255;
  CAMLreturn (Val_int(retcode));
}
#else
CAMLprim value caml_sys_system_command(value command)
{
  caml_invalid_argument("Sys.command not implemented");
}
#endif

double caml_sys_time_include_children_unboxed(value include_children)
{
#ifdef HAS_GETRUSAGE
  struct rusage ru;
  double acc = 0.;

  getrusage (RUSAGE_SELF, &ru);
  acc += ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6
    + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;

  if (Bool_val(include_children)) {
    getrusage (RUSAGE_CHILDREN, &ru);
    acc += ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6
      + ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;
  }

  return acc;
#else
  #ifdef HAS_TIMES
    #ifndef CLK_TCK
      #ifdef HZ
        #define CLK_TCK HZ
      #else
        #define CLK_TCK 60
      #endif
    #endif
    struct tms t;
    clock_t acc = 0;
    times(&t);
    acc += t.tms_utime + t.tms_stime;
    if (Bool_val(include_children)) {
      acc += t.tms_cutime + t.tms_cstime;
    }
    return (double)acc / CLK_TCK;
  #else
    /* clock() is standard ANSI C. We have no way of getting
       subprocess times in this branch. */
    return (double)clock_os() / CLOCKS_PER_SEC;
  #endif
#endif
}

CAMLprim value caml_sys_time_include_children(value include_children)
{
  return caml_copy_double(
      caml_sys_time_include_children_unboxed(include_children));
}

double caml_sys_time_unboxed(value unit) {
  return caml_sys_time_include_children_unboxed(Val_false);
}

CAMLprim value caml_sys_time(value unit)
{
  return caml_copy_double(caml_sys_time_unboxed(unit));
}

#ifdef _WIN32
extern int caml_win32_random_seed (intnat data[16]);
#else
int caml_unix_random_seed(intnat data[16])
{
  int fd;
  int n = 0;

  /* Try /dev/urandom first */
  fd = open("/dev/urandom", O_RDONLY, 0);
  if (fd != -1) {
    unsigned char buffer[12];
    int nread = read(fd, buffer, 12);
    close(fd);
    while (nread > 0) data[n++] = buffer[--nread];
  }
  /* If the read from /dev/urandom fully succeeded, we now have 96 bits
    of good random data and can stop here. */
  if (n >= 12) return n;
  /* Otherwise, complement whatever we got (probably nothing)
     with some not-very-random data. */
  {
#ifdef HAS_GETTIMEOFDAY
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (n < 16) data[n++] = tv.tv_usec;
    if (n < 16) data[n++] = tv.tv_sec;
#else
    if (n < 16) data[n++] = time(NULL);
#endif
    if (n < 16) data[n++] = (intnat)Caml_state->unique_token_root;
#ifdef HAS_UNISTD
    if (n < 16) data[n++] = getpid();
    if (n < 16) data[n++] = getppid();
#endif
    return n;
  }
}
#endif

CAMLprim value caml_sys_random_seed (value unit)
{
  intnat data[16];
  int n, i;
  value res;
#ifdef _WIN32
  n = caml_win32_random_seed(data);
#else
  n = caml_unix_random_seed(data);
#endif
  /* Convert to an OCaml array of ints */
  res = caml_alloc_small(n, 0);
  for (i = 0; i < n; i++) Field(res, i) = Val_long(data[i]);
  return res;
}

CAMLprim value caml_sys_const_big_endian(value unit)
{
#ifdef ARCH_BIG_ENDIAN
  return Val_true;
#else
  return Val_false;
#endif
}

/* returns a value that represents a number of bits */
CAMLprim value caml_sys_const_word_size(value unit)
{
  return Val_long(8 * sizeof(value));
}

/* returns a value that represents a number of bits */
CAMLprim value caml_sys_const_int_size(value unit)
{
  return Val_long(8 * sizeof(value) - 1) ;
}

/* returns a value that represents a number of words */
CAMLprim value caml_sys_const_max_wosize(value unit)
{
  return Val_long(Max_wosize) ;
}

CAMLprim value caml_sys_const_ostype_unix(value unit)
{
  return Val_bool(0 == strcmp(OCAML_OS_TYPE,"Unix"));
}

CAMLprim value caml_sys_const_ostype_win32(value unit)
{
  return Val_bool(0 == strcmp(OCAML_OS_TYPE,"Win32"));
}

CAMLprim value caml_sys_const_ostype_cygwin(value unit)
{
  return Val_bool(0 == strcmp(OCAML_OS_TYPE,"Cygwin"));
}

CAMLprim value caml_sys_const_backend_type(value unit)
{
  return Val_int(1); /* Bytecode backed */
}
CAMLprim value caml_sys_get_config(value unit)
{
  CAMLparam0 ();   /* unit is unused */
  CAMLlocal2 (result, ostype);

  ostype = caml_copy_string(OCAML_OS_TYPE);
  result = caml_alloc_small (3, 0);
  Field(result, 0) = ostype;
  Field(result, 1) = Val_long (8 * sizeof(value));
#ifdef ARCH_BIG_ENDIAN
  Field(result, 2) = Val_true;
#else
  Field(result, 2) = Val_false;
#endif
  CAMLreturn (result);
}

CAMLprim value caml_sys_read_directory(value path)
{
  CAMLparam1(path);
  CAMLlocal1(result);
  struct ext_table tbl;
  char_os * p;
  int ret;

  caml_sys_check_path(path);
  caml_ext_table_init(&tbl, 50);
  p = caml_stat_strdup_to_os(String_val(path));
  caml_enter_blocking_section();
  ret = caml_read_directory(p, &tbl);
  caml_leave_blocking_section();
  caml_stat_free(p);
  if (ret == -1){
    caml_ext_table_free(&tbl, 1);
    caml_sys_error(path);
  }
  caml_ext_table_add(&tbl, NULL);
  result = caml_copy_string_array((char const **) tbl.contents);
  caml_ext_table_free(&tbl, 1);
  CAMLreturn(result);
}

/* Return true if the value is a filedescriptor (int) that is
 * (presumably) open on an interactive terminal */
CAMLprim value caml_sys_isatty(value chan)
{
  int fd;
  value ret;

  fd = (Channel(chan))->fd;
#ifdef _WIN32
  ret = Val_bool(caml_win32_isatty(fd));
#else
  ret = Val_bool(isatty(fd));
#endif

  return ret;
}

CAMLprim value caml_sys_const_naked_pointers_checked(value unit)
{
#ifdef NAKED_POINTERS_CHECKER
  return Val_true;
#else
  return Val_false;
#endif
}
