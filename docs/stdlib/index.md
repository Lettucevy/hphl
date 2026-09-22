# HP-HL Standard Library Reference

> **Auto-generated** from `compiler/src/stdlib/stdbuiltins.cpp` by `tools/gen_stdlib_doc.py`.
> Do not edit by hand; re-run the script after changing the table.

Total: **126** builtins across **7** categories.

## Table of Contents

- [Math](#math)
- [Other](#other)
- [String](#string)
- [File IO](#file-io)
- [System](#system)
- [Time](#time)
- [Socket](#socket)

### Math

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `abs` | `abs(int)` | `int` | [`hphl_abs_i64`](runtime.md#hphl_abs_i64) |
| `min` | `min(int, int)` | `int` | [`hphl_min_i64`](runtime.md#hphl_min_i64) |
| `sqrt` | `sqrt(float)` | `float` | [`hphl_sqrt`](runtime.md#hphl_sqrt) |

### Other

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `abs` | `abs(float)` | `float` | [`hphl_abs_f64`](runtime.md#hphl_abs_f64) |
| `pow` | `pow(float, float)` | `float` | [`hphl_pow`](runtime.md#hphl_pow) |
| `max` | `max(int, int)` | `int` | [`hphl_max_i64`](runtime.md#hphl_max_i64) |
| `min` | `min(float, float)` | `float` | [`fmin`](runtime.md#fmin) |
| `max` | `max(float, float)` | `float` | [`fmax`](runtime.md#fmax) |
| `floor` | `floor(float)` | `float` | [`hphl_floor`](runtime.md#hphl_floor) |
| `ceil` | `ceil(float)` | `float` | [`hphl_ceil`](runtime.md#hphl_ceil) |
| `round` | `round(float)` | `float` | [`hphl_round`](runtime.md#hphl_round) |
| `fmod` | `fmod(float, float)` | `float` | [`hphl_fmod`](runtime.md#hphl_fmod) |
| `PI` | `PI()` | `float` | [`hphl_pi`](runtime.md#hphl_pi) |
| `E` | `E()` | `float` | [`hphl_e`](runtime.md#hphl_e) |
| `sin` | `sin(float)` | `float` | [`hphl_sin`](runtime.md#hphl_sin) |
| `cos` | `cos(float)` | `float` | [`hphl_cos`](runtime.md#hphl_cos) |
| `tan` | `tan(float)` | `float` | [`hphl_tan`](runtime.md#hphl_tan) |
| `log` | `log(float)` | `float` | [`hphl_log`](runtime.md#hphl_log) |
| `exp` | `exp(float)` | `float` | [`hphl_exp`](runtime.md#hphl_exp) |
| `trunc` | `trunc(float)` | `float` | [`hphl_trunc`](runtime.md#hphl_trunc) |
| `asin` | `asin(float)` | `float` | [`hphl_asin`](runtime.md#hphl_asin) |
| `acos` | `acos(float)` | `float` | [`hphl_acos`](runtime.md#hphl_acos) |
| `atan` | `atan(float)` | `float` | [`hphl_atan`](runtime.md#hphl_atan) |
| `atan2` | `atan2(float, float)` | `float` | [`hphl_atan2`](runtime.md#hphl_atan2) |
| `log2` | `log2(float)` | `float` | [`hphl_log2`](runtime.md#hphl_log2) |
| `exp2` | `exp2(float)` | `float` | [`hphl_exp2`](runtime.md#hphl_exp2) |
| `ord` | `ord(Str, int)` | `int` | [`hphl_str_ord_at`](runtime.md#hphl_str_ord_at) |
| `chr` | `chr(int)` | `Str` | [`hphl_str_chr`](runtime.md#hphl_str_chr) |
| `up_case` | `up_case(Str)` | `Str` | [`hphl_str_up`](runtime.md#hphl_str_up) |
| `down_case` | `down_case(Str)` | `Str` | [`hphl_str_down`](runtime.md#hphl_str_down) |
| `toUpper` | `toUpper(Str)` | `Str` | [`hphl_str_up`](runtime.md#hphl_str_up) |
| `toLower` | `toLower(Str)` | `Str` | [`hphl_str_down`](runtime.md#hphl_str_down) |
| `trim` | `trim(Str)` | `Str` | [`hphl_str_trim`](runtime.md#hphl_str_trim) |
| `padLeft` | `padLeft(Str, int, Str)` | `Str` | [`hphl_str_pad_left`](runtime.md#hphl_str_pad_left) |
| `padRight` | `padRight(Str, int, Str)` | `Str` | [`hphl_str_pad_right`](runtime.md#hphl_str_pad_right) |
| `format` | `format(Str, Str)` | `Str` | [`hphl_str_format`](runtime.md#hphl_str_format) |
| `format` | `format(Str, int)` | `Str` | [`hphl_str_format_int`](runtime.md#hphl_str_format_int) |
| `format` | `format(Str, float)` | `Str` | [`hphl_str_format_float`](runtime.md#hphl_str_format_float) |
| `join` | `join(list, Str)` | `Str` | [`hphl_join`](runtime.md#hphl_join) |
| `parse_int` | `parse_int(Str)` | `int` | [`hphl_parse_int`](runtime.md#hphl_parse_int) |
| `parse_float` | `parse_float(Str)` | `float` | [`hphl_parse_f64`](runtime.md#hphl_parse_f64) |
| `is_numeric` | `is_numeric(Str)` | `bool` | [`hphl_is_numeric`](runtime.md#hphl_is_numeric) |
| `hphl_gas_str` | `hphl_gas_str(Str)` | `Str` | [`hphl_gas_str`](runtime.md#hphl_gas_str) |
| `gas_str` | `gas_str(Str)` | `Str` | [`hphl_gas_str`](runtime.md#hphl_gas_str) |
| `readline` | `readline()` | `Str` | [`hphl_read_line`](runtime.md#hphl_read_line) |
| `file_exists` | `file_exists(Str)` | `bool` | [`hphl_file_exists`](runtime.md#hphl_file_exists) |
| `remove_file` | `remove_file(Str)` | `bool` | [`hphl_remove_file`](runtime.md#hphl_remove_file) |
| `list_dir` | `list_dir(Str)` | `list` | [`hphl_list_dir`](runtime.md#hphl_list_dir) |
| `read_lines` | `read_lines(Str)` | `list` | [`hphl_read_lines`](runtime.md#hphl_read_lines) |
| `write_lines` | `write_lines(Str, list)` | `bool` | [`hphl_write_lines`](runtime.md#hphl_write_lines) |
| `stat` | `stat(Str)` | `Str` | [`hphl_stat`](runtime.md#hphl_stat) |
| `json_stringify` | `json_stringify(Str)` | `Str` | [`hphl_json_stringify`](runtime.md#hphl_json_stringify) |
| `json_get` | `json_get(Str, Str)` | `Str` | [`hphl_json_get`](runtime.md#hphl_json_get) |
| `json_set` | `json_set(Str, Str, Str)` | `Str` | [`hphl_json_set`](runtime.md#hphl_json_set) |
| `random` | `random()` | `float` | [`hphl_random`](runtime.md#hphl_random) |
| `sleep_ms` | `sleep_ms(int)` | `int` | [`hphl_sleep_ms`](runtime.md#hphl_sleep_ms) |
| `env_var` | `env_var(Str)` | `Str` | [`hphl_env_var`](runtime.md#hphl_env_var) |
| `exit_prog` | `exit_prog(int)` | `int` | [`hphl_exit_prog`](runtime.md#hphl_exit_prog) |
| `set_env` | `set_env(Str, Str)` | `int` | [`hphl_set_env`](runtime.md#hphl_set_env) |
| `abort` | `abort()` | `void` | [`hphl_abort`](runtime.md#hphl_abort) |
| `assert` | `assert(int, Str)` | `void` | [`hphl_assert`](runtime.md#hphl_assert) |
| `now_ms` | `now_ms()` | `int` | [`hphl_now_ms`](runtime.md#hphl_now_ms) |
| `now_us` | `now_us()` | `int` | [`hphl_now_us`](runtime.md#hphl_now_us) |
| `formatDate` | `formatDate(int, Str)` | `Str` | [`hphl_format_date`](runtime.md#hphl_format_date) |
| `parseDate` | `parseDate(Str, Str)` | `int` | [`hphl_parse_date`](runtime.md#hphl_parse_date) |
| `bind` | `bind(int, int)` | `int` | [`hphl_bind`](runtime.md#hphl_bind) |
| `connect` | `connect(int, Str, int)` | `int` | [`hphl_connect`](runtime.md#hphl_connect) |
| `listen` | `listen(int, int)` | `int` | [`hphl_listen`](runtime.md#hphl_listen) |
| `accept` | `accept(int)` | `int` | [`hphl_accept`](runtime.md#hphl_accept) |
| `send` | `send(int, Str)` | `int` | [`hphl_send`](runtime.md#hphl_send) |
| `recv` | `recv(int, int)` | `Str` | [`hphl_recv`](runtime.md#hphl_recv) |
| `close_socket` | `close_socket(int)` | `int` | [`hphl_close_socket`](runtime.md#hphl_close_socket) |
| `gc_pressure` | `gc_pressure()` | `int` | [`hphl_gc_pressure`](runtime.md#hphl_gc_pressure) |
| `gc_collections` | `gc_collections()` | `int` | [`hphl_gc_stats_collections`](runtime.md#hphl_gc_stats_collections) |
| `gc_freed` | `gc_freed()` | `int` | [`hphl_gc_stats_freed`](runtime.md#hphl_gc_stats_freed) |
| `gc_max_pause_ns` | `gc_max_pause_ns()` | `int` | [`hphl_gc_stats_max_pause`](runtime.md#hphl_gc_stats_max_pause) |
| `gc_last_freed` | `gc_last_freed()` | `int` | [`hphl_gc_stats_last_freed`](runtime.md#hphl_gc_stats_last_freed) |
| `mutex_new` | `mutex_new()` | `ptr` | [`hphl_mutex_new`](runtime.md#hphl_mutex_new) |
| `mutex_lock` | `mutex_lock(ptr)` | `void` | [`hphl_mutex_lock`](runtime.md#hphl_mutex_lock) |
| `mutex_unlock` | `mutex_unlock(ptr)` | `void` | [`hphl_mutex_unlock`](runtime.md#hphl_mutex_unlock) |
| `mutex_destroy` | `mutex_destroy(ptr)` | `void` | [`hphl_mutex_destroy`](runtime.md#hphl_mutex_destroy) |
| `condvar_new` | `condvar_new()` | `ptr` | [`hphl_condvar_new`](runtime.md#hphl_condvar_new) |
| `condvar_wait` | `condvar_wait(ptr, ptr)` | `void` | [`hphl_condvar_wait`](runtime.md#hphl_condvar_wait) |
| `condvar_signal` | `condvar_signal(ptr)` | `void` | [`hphl_condvar_signal`](runtime.md#hphl_condvar_signal) |
| `condvar_broadcast` | `condvar_broadcast(ptr)` | `void` | [`hphl_condvar_broadcast`](runtime.md#hphl_condvar_broadcast) |
| `condvar_destroy` | `condvar_destroy(ptr)` | `void` | [`hphl_condvar_destroy`](runtime.md#hphl_condvar_destroy) |
| `atomic_load_i64` | `atomic_load_i64(ptr)` | `int` | [`hphl_atomic_load_i64`](runtime.md#hphl_atomic_load_i64) |
| `atomic_store_i64` | `atomic_store_i64(ptr, int)` | `void` | [`hphl_atomic_store_i64`](runtime.md#hphl_atomic_store_i64) |
| `atomic_add_i64` | `atomic_add_i64(ptr, int)` | `int` | [`hphl_atomic_add_i64`](runtime.md#hphl_atomic_add_i64) |
| `atomic_sub_i64` | `atomic_sub_i64(ptr, int)` | `int` | [`hphl_atomic_sub_i64`](runtime.md#hphl_atomic_sub_i64) |
| `atomic_cas_i64` | `atomic_cas_i64(ptr, int, int)` | `int` | [`hphl_atomic_cas_i64`](runtime.md#hphl_atomic_cas_i64) |
| `img_grayscale` | `img_grayscale(list)` | `ListInt` | [`hphl_img_grayscale`](runtime.md#hphl_img_grayscale) |
| `img_flip_h` | `img_flip_h(list, int, int)` | `ListInt` | [`hphl_img_flip_h`](runtime.md#hphl_img_flip_h) |
| `img_resize` | `img_resize(list, int, int, int, int)` | `ListInt` | [`hphl_img_resize`](runtime.md#hphl_img_resize) |
| `img_blur` | `img_blur(list, int, int, int)` | `ListInt` | [`hphl_img_blur`](runtime.md#hphl_img_blur) |
| `img_ppm_save` | `img_ppm_save(Str, list, int, int)` | `bool` | [`hphl_img_ppm_save`](runtime.md#hphl_img_ppm_save) |
| `img_ppm_load` | `img_ppm_load(Str)` | `ListInt` | [`hphl_img_ppm_load`](runtime.md#hphl_img_ppm_load) |
| `img_png_save` | `img_png_save(Str, list, int, int)` | `bool` | [`hphl_img_png_save`](runtime.md#hphl_img_png_save) |
| `mem_alloc` | `mem_alloc(int)` | `ptr` | [`hphl_mem_alloc`](runtime.md#hphl_mem_alloc) |
| `mem_free` | `mem_free(ptr)` | `void` | [`hphl_mem_free`](runtime.md#hphl_mem_free) |
| `mem_poke_i32` | `mem_poke_i32(ptr, int, int)` | `void` | [`hphl_mem_poke_i32`](runtime.md#hphl_mem_poke_i32) |
| `mem_peek_i32` | `mem_peek_i32(ptr, int)` | `int` | [`hphl_mem_peek_i32`](runtime.md#hphl_mem_peek_i32) |
| `mem_poke_u32` | `mem_poke_u32(ptr, int, int)` | `void` | [`hphl_mem_poke_u32`](runtime.md#hphl_mem_poke_u32) |
| `mem_peek_u32` | `mem_peek_u32(ptr, int)` | `int` | [`hphl_mem_peek_u32`](runtime.md#hphl_mem_peek_u32) |
| `mem_poke_i64` | `mem_poke_i64(ptr, int, int)` | `void` | [`hphl_mem_poke_i64`](runtime.md#hphl_mem_poke_i64) |
| `mem_peek_i64` | `mem_peek_i64(ptr, int)` | `int` | [`hphl_mem_peek_i64`](runtime.md#hphl_mem_peek_i64) |
| `mem_poke_ptr` | `mem_poke_ptr(ptr, int, ptr)` | `void` | [`hphl_mem_poke_ptr`](runtime.md#hphl_mem_poke_ptr) |
| `mem_peek_ptr` | `mem_peek_ptr(ptr, int)` | `ptr` | [`hphl_mem_peek_ptr`](runtime.md#hphl_mem_peek_ptr) |
| `mem_poke_f32` | `mem_poke_f32(ptr, int, float)` | `void` | [`hphl_mem_poke_f32`](runtime.md#hphl_mem_poke_f32) |
| `mem_peek_f32` | `mem_peek_f32(ptr, int)` | `float` | [`hphl_mem_peek_f32`](runtime.md#hphl_mem_peek_f32) |
| `mem_poke_f64` | `mem_poke_f64(ptr, int, float)` | `void` | [`hphl_mem_poke_f64`](runtime.md#hphl_mem_poke_f64) |
| `mem_peek_f64` | `mem_peek_f64(ptr, int)` | `float` | [`hphl_mem_peek_f64`](runtime.md#hphl_mem_peek_f64) |
| `mem_copy` | `mem_copy(ptr, ptr, int)` | `void` | [`hphl_mem_copy`](runtime.md#hphl_mem_copy) |
| `mem_copy_off` | `mem_copy_off(ptr, int, ptr, int, int)` | `void` | [`hphl_mem_copy_off`](runtime.md#hphl_mem_copy_off) |
| `mem_fill` | `mem_fill(ptr, int, int)` | `void` | [`hphl_mem_fill`](runtime.md#hphl_mem_fill) |
| `mem_zero` | `mem_zero(ptr, int)` | `void` | [`hphl_mem_zero`](runtime.md#hphl_mem_zero) |

### String

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `str_len` | `str_len(Str)` | `int` | [`hphl_str_len`](runtime.md#hphl_str_len) |
| `ord` | `ord(Str)` | `int` | [`hphl_str_ord`](runtime.md#hphl_str_ord) |
| `split` | `split(Str, Str)` | `list` | [`hphl_split`](runtime.md#hphl_split) |
| `json_parse` | `json_parse(Str)` | `Str` | [`hphl_json_parse`](runtime.md#hphl_json_parse) |

### File IO

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `read_line` | `read_line()` | `Str` | [`hphl_read_line`](runtime.md#hphl_read_line) |
| `read_file` | `read_file(Str)` | `Str` | [`hphl_read_file`](runtime.md#hphl_read_file) |
| `mkdir` | `mkdir(Str)` | `bool` | [`hphl_mkdir`](runtime.md#hphl_mkdir) |

### System

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `args` | `args()` | `list` | [`hphl_args`](runtime.md#hphl_args) |

### Time

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `now` | `now()` | `int` | [`hphl_now`](runtime.md#hphl_now) |

### Socket

| Name | Signature | Returns | Runtime |
|------|-----------|---------|---------|
| `socket` | `socket(int, int, int)` | `int` | [`hphl_socket`](runtime.md#hphl_socket) |

