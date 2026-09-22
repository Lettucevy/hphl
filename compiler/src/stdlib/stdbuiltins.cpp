// stdbuiltins.cpp — M12.0 (v0.54.0): tabela da biblioteca padrão.
// Adicionar uma função = uma entrada aqui + implementação em runtime.c.
// Grupos: math (v0.54.1), string query/transform (v0.54.2-3), file IO
// (v0.54.4), extras (v0.54.5).
#include "stdbuiltins.h"


namespace hphl {

const std::vector<StdBuiltin>& stdBuiltinTable() {
  static const std::vector<StdBuiltin> table = {
      // ---- piloto M12.0: abs(int)/abs(double) + pow (libm) ----
      {"abs", {SBType::Int}, SBType::Int, "hphl_abs_i64"},
      {"abs", {SBType::Float}, SBType::Float, "hphl_abs_f64"},
      {"pow", {SBType::Float, SBType::Float}, SBType::Float, "hphl_pow"},
      // ---- M12.1 (v0.54.1): math completo ----
      {"min", {SBType::Int, SBType::Int}, SBType::Int, "hphl_min_i64"},
      {"max", {SBType::Int, SBType::Int}, SBType::Int, "hphl_max_i64"},
      {"min", {SBType::Float, SBType::Float}, SBType::Float, "fmin"},
      {"max", {SBType::Float, SBType::Float}, SBType::Float, "fmax"},
      {"floor", {SBType::Float}, SBType::Float, "hphl_floor"},   // wrapper
      {"ceil", {SBType::Float}, SBType::Float, "hphl_ceil"},     // wrapper
      {"round", {SBType::Float}, SBType::Float, "hphl_round"},   // wrapper
      {"fmod", {SBType::Float, SBType::Float}, SBType::Float, "hphl_fmod"}, // wrapper
      {"PI", {}, SBType::Float, "hphl_pi"},
      {"E", {}, SBType::Float, "hphl_e"},
      // M27 math:
      {"sqrt", {SBType::Float}, SBType::Float, "hphl_sqrt"},
      {"sin", {SBType::Float}, SBType::Float, "hphl_sin"},
      {"cos", {SBType::Float}, SBType::Float, "hphl_cos"},
      {"tan", {SBType::Float}, SBType::Float, "hphl_tan"},
      {"log", {SBType::Float}, SBType::Float, "hphl_log"},
      {"exp", {SBType::Float}, SBType::Float, "hphl_exp"},
      {"trunc", {SBType::Float}, SBType::Float, "hphl_trunc"},
      {"asin", {SBType::Float}, SBType::Float, "hphl_asin"},
      {"acos", {SBType::Float}, SBType::Float, "hphl_acos"},
      {"atan", {SBType::Float}, SBType::Float, "hphl_atan"},
      {"atan2", {SBType::Float, SBType::Float}, SBType::Float, "hphl_atan2"},
      {"log2", {SBType::Float}, SBType::Float, "hphl_log2"},
      {"exp2", {SBType::Float}, SBType::Float, "hphl_exp2"},
      // ---- M12.2 (v0.54.2): string query ----
      {"str_len", {SBType::Str}, SBType::Int, "hphl_str_len"},
      {"sub_str", {SBType::Str, SBType::Int, SBType::Int}, SBType::Str,
       "hphl_str_sub"},
      {"index_of", {SBType::Str, SBType::Str}, SBType::Int,
       "hphl_str_index_of"},
      // M27 string:
      {"char_at", {SBType::Str, SBType::Int}, SBType::Str,
       "hphl_str_char_at"},
      {"charAt", {SBType::Str, SBType::Int}, SBType::Str,
       "hphl_str_char_at"},
      {"ord", {SBType::Str}, SBType::Int, "hphl_str_ord"},
      {"ord", {SBType::Str, SBType::Int}, SBType::Int, "hphl_str_ord_at"},
      {"chr", {SBType::Int}, SBType::Str, "hphl_str_chr"},
      {"last_index_of", {SBType::Str, SBType::Str}, SBType::Int,
       "hphl_str_last_index_of"},
      {"lastIndexOf", {SBType::Str, SBType::Str}, SBType::Int,
       "hphl_str_last_index_of"},
      {"contains", {SBType::Str, SBType::Str}, SBType::Bool,
       "hphl_str_contains"},
      {"starts_with", {SBType::Str, SBType::Str}, SBType::Bool,
       "hphl_str_starts"},
      {"ends_with", {SBType::Str, SBType::Str}, SBType::Bool,
       "hphl_str_ends"},
      {"up_case", {SBType::Str}, SBType::Str, "hphl_str_up"},
      {"down_case", {SBType::Str}, SBType::Str, "hphl_str_down"},
      {"toUpper", {SBType::Str}, SBType::Str, "hphl_str_up"},
      {"toLower", {SBType::Str}, SBType::Str, "hphl_str_down"},
      {"trim", {SBType::Str}, SBType::Str, "hphl_str_trim"},
      {"padLeft", {SBType::Str, SBType::Int, SBType::Str}, SBType::Str, "hphl_str_pad_left"},
      {"padRight", {SBType::Str, SBType::Int, SBType::Str}, SBType::Str, "hphl_str_pad_right"},
      {"format", {SBType::Str, SBType::Str}, SBType::Str, "hphl_str_format"},
      {"format", {SBType::Str, SBType::Int}, SBType::Str, "hphl_str_format_int"},
      {"format", {SBType::Str, SBType::Float}, SBType::Str, "hphl_str_format_float"},
      // ---- M12.3 (v0.54.3): string transform + parse ----
      {"replace", {SBType::Str, SBType::Str, SBType::Str}, SBType::Str,
       "hphl_str_replace"},
      {"split", {SBType::Str, SBType::Str}, SBType::List, "hphl_split"},
      {"join", {SBType::List, SBType::Str}, SBType::Str, "hphl_join"},
      {"parse_int", {SBType::Str}, SBType::Int, "hphl_parse_int"},
      {"parse_float", {SBType::Str}, SBType::Float, "hphl_parse_f64"},
      {"is_numeric", {SBType::Str}, SBType::Bool, "hphl_is_numeric"},
      {"hphl_gas_str", {SBType::Str}, SBType::Str, "hphl_gas_str"},
      {"gas_str", {SBType::Str}, SBType::Str, "hphl_gas_str"},
      // Console IO
      {"read_line", {}, SBType::Str, "hphl_read_line"},
      {"readline", {}, SBType::Str, "hphl_read_line"},
      // ---- M12.4 (v0.54.4): file IO ----
      {"read_file", {SBType::Str}, SBType::Str, "hphl_read_file"},
      {"write_file", {SBType::Str, SBType::Str}, SBType::Bool,
       "hphl_write_file"},
      {"append_file", {SBType::Str, SBType::Str}, SBType::Bool,
       "hphl_append_file"},
      {"file_exists", {SBType::Str}, SBType::Bool, "hphl_file_exists"},
      {"remove_file", {SBType::Str}, SBType::Bool, "hphl_remove_file"},
      // M27 file:
      {"mkdir", {SBType::Str}, SBType::Bool, "hphl_mkdir"},
      {"list_dir", {SBType::Str}, SBType::List, "hphl_list_dir"},
      {"read_lines", {SBType::Str}, SBType::List, "hphl_read_lines"},
      {"write_lines", {SBType::Str, SBType::List}, SBType::Bool, "hphl_write_lines"},
      {"stat", {SBType::Str}, SBType::Str, "hphl_stat"},
      // M_RV1 G5: JSON (parse/stringify already via json.cpp, get/set new)
      {"json_parse", {SBType::Str}, SBType::Str, "hphl_json_parse"},
      {"json_stringify", {SBType::Str}, SBType::Str, "hphl_json_stringify"},
      {"json_get", {SBType::Str, SBType::Str}, SBType::Str, "hphl_json_get"},
      {"json_set", {SBType::Str, SBType::Str, SBType::Str}, SBType::Str, "hphl_json_set"},
      // ---- M12.5 (v0.54.5): extras ----
      {"random", {}, SBType::Float, "hphl_random"},
      {"random_int", {SBType::Int, SBType::Int}, SBType::Int,
       "hphl_random_int"},
      {"sleep_ms", {SBType::Int}, SBType::Int, "hphl_sleep_ms"},
      {"env_var", {SBType::Str}, SBType::Str, "hphl_env_var"},
      {"exit_prog", {SBType::Int}, SBType::Int, "hphl_exit_prog"},
      // M27 system:
      {"args", {}, SBType::List, "hphl_args"},
      {"set_env", {SBType::Str, SBType::Str}, SBType::Int, "hphl_set_env"},
      {"abort", {}, SBType::Void, "hphl_abort"},
      {"assert", {SBType::Int, SBType::Str}, SBType::Void, "hphl_assert"},
      // M27 datetime:
      {"now", {}, SBType::Int, "hphl_now"},
      {"now_ms", {}, SBType::Int, "hphl_now_ms"},
      {"now_us", {}, SBType::Int, "hphl_now_us"},
      {"formatDate", {SBType::Int, SBType::Str}, SBType::Str, "hphl_format_date"},
      {"parseDate", {SBType::Str, SBType::Str}, SBType::Int, "hphl_parse_date"},
      // M_RV1 G7: socket
      {"socket", {SBType::Int, SBType::Int, SBType::Int}, SBType::Int, "hphl_socket"},
      {"bind", {SBType::Int, SBType::Int}, SBType::Int, "hphl_bind"},
      {"connect", {SBType::Int, SBType::Str, SBType::Int}, SBType::Int, "hphl_connect"},
      {"listen", {SBType::Int, SBType::Int}, SBType::Int, "hphl_listen"},
      {"accept", {SBType::Int}, SBType::Int, "hphl_accept"},
      {"send", {SBType::Int, SBType::Str}, SBType::Int, "hphl_send"},
      {"recv", {SBType::Int, SBType::Int}, SBType::Str, "hphl_recv"},
      {"close_socket", {SBType::Int}, SBType::Int, "hphl_close_socket"},
      // ---- M20 1.4 ----
      {"gc_pressure", {}, SBType::Int, "hphl_gc_pressure"},
      // ---- M31: estatísticas do GC (leitura; sem alocação) ----
      {"gc_collections", {}, SBType::Int, "hphl_gc_stats_collections"},
      {"gc_freed", {}, SBType::Int, "hphl_gc_stats_freed"},
      {"gc_max_pause_ns", {}, SBType::Int, "hphl_gc_stats_max_pause"},
      {"gc_last_freed", {}, SBType::Int, "hphl_gc_stats_last_freed"},
      // ---- M_RV1 F11: mutex, condvar, atomic ----
      // mutex/condvar/atomic_int_ptr: handle opaco de 8 bytes (tipo Ptr).
      // O codegen deve passar o ENDEREÇO do slot (Ptr), não o valor.
      // atomic intrinsics operam no endereco passado (volatile long long*).
      {"mutex_new", {}, SBType::Ptr, "hphl_mutex_new"},
      {"mutex_lock", {SBType::Ptr}, SBType::Void, "hphl_mutex_lock"},
      {"mutex_unlock", {SBType::Ptr}, SBType::Void, "hphl_mutex_unlock"},
      {"mutex_destroy", {SBType::Ptr}, SBType::Void, "hphl_mutex_destroy"},
      {"condvar_new", {}, SBType::Ptr, "hphl_condvar_new"},
      {"condvar_wait", {SBType::Ptr, SBType::Ptr}, SBType::Void, "hphl_condvar_wait"},
      {"condvar_signal", {SBType::Ptr}, SBType::Void, "hphl_condvar_signal"},
      {"condvar_broadcast", {SBType::Ptr}, SBType::Void, "hphl_condvar_broadcast"},
      {"condvar_destroy", {SBType::Ptr}, SBType::Void, "hphl_condvar_destroy"},
      {"atomic_load_i64", {SBType::Ptr}, SBType::Int, "hphl_atomic_load_i64"},
      {"atomic_store_i64", {SBType::Ptr, SBType::Int}, SBType::Void, "hphl_atomic_store_i64"},
      {"atomic_add_i64", {SBType::Ptr, SBType::Int}, SBType::Int, "hphl_atomic_add_i64"},
      {"atomic_sub_i64", {SBType::Ptr, SBType::Int}, SBType::Int, "hphl_atomic_sub_i64"},
      {"atomic_cas_i64", {SBType::Ptr, SBType::Int, SBType::Int}, SBType::Int, "hphl_atomic_cas_i64"},
      // ---- M_RV1 IMG: manipulação de imagens (list<int> RGB flat + w/h) ----
      {"img_grayscale", {SBType::List}, SBType::ListInt, "hphl_img_grayscale"},
      {"img_flip_h", {SBType::List, SBType::Int, SBType::Int}, SBType::ListInt, "hphl_img_flip_h"},
      {"img_resize", {SBType::List, SBType::Int, SBType::Int, SBType::Int, SBType::Int}, SBType::ListInt, "hphl_img_resize"},
      {"img_blur", {SBType::List, SBType::Int, SBType::Int, SBType::Int}, SBType::ListInt, "hphl_img_blur"},
      {"img_ppm_save", {SBType::Str, SBType::List, SBType::Int, SBType::Int}, SBType::Bool, "hphl_img_ppm_save"},
      {"img_ppm_load", {SBType::Str}, SBType::ListInt, "hphl_img_ppm_load"},
      {"img_png_save", {SBType::Str, SBType::List, SBType::Int, SBType::Int}, SBType::Bool, "hphl_img_png_save"},
      // ---- FFI v2: memória raw p/ interop C/Vulkan (montar structs byte a byte) ----
      // Ponteiros NÃO são gerenciados pelo GC: alloc/free manuais.
      {"mem_alloc", {SBType::Int}, SBType::Ptr, "hphl_mem_alloc"},
      {"mem_free", {SBType::Ptr}, SBType::Void, "hphl_mem_free"},
      {"mem_poke_i32", {SBType::Ptr, SBType::Int, SBType::Int}, SBType::Void, "hphl_mem_poke_i32"},
      {"mem_peek_i32", {SBType::Ptr, SBType::Int}, SBType::Int, "hphl_mem_peek_i32"},
      {"mem_poke_u32", {SBType::Ptr, SBType::Int, SBType::Int}, SBType::Void, "hphl_mem_poke_u32"},
      {"mem_peek_u32", {SBType::Ptr, SBType::Int}, SBType::Int, "hphl_mem_peek_u32"},
      {"mem_poke_i64", {SBType::Ptr, SBType::Int, SBType::Int}, SBType::Void, "hphl_mem_poke_i64"},
      {"mem_peek_i64", {SBType::Ptr, SBType::Int}, SBType::Int, "hphl_mem_peek_i64"},
      {"mem_poke_ptr", {SBType::Ptr, SBType::Int, SBType::Ptr}, SBType::Void, "hphl_mem_poke_ptr"},
      {"mem_peek_ptr", {SBType::Ptr, SBType::Int}, SBType::Ptr, "hphl_mem_peek_ptr"},
      {"mem_poke_f32", {SBType::Ptr, SBType::Int, SBType::Float}, SBType::Void, "hphl_mem_poke_f32"},
      {"mem_peek_f32", {SBType::Ptr, SBType::Int}, SBType::Float, "hphl_mem_peek_f32"},
      {"mem_poke_f64", {SBType::Ptr, SBType::Int, SBType::Float}, SBType::Void, "hphl_mem_poke_f64"},
      {"mem_peek_f64", {SBType::Ptr, SBType::Int}, SBType::Float, "hphl_mem_peek_f64"},
      {"mem_copy", {SBType::Ptr, SBType::Ptr, SBType::Int}, SBType::Void, "hphl_mem_copy"},
      {"mem_copy_off", {SBType::Ptr, SBType::Int, SBType::Ptr, SBType::Int, SBType::Int}, SBType::Void, "hphl_mem_copy_off"},
      {"mem_fill", {SBType::Ptr, SBType::Int, SBType::Int}, SBType::Void, "hphl_mem_fill"},
      {"mem_zero", {SBType::Ptr, SBType::Int}, SBType::Void, "hphl_mem_zero"},
  };
  return table;
}

int findStdBuiltin(const std::string& name, size_t argc,
                   const std::vector<SBType>& argKinds) {
  int best = -1;
  int bestScore = -1;
  const auto& table = stdBuiltinTable();
  for (size_t i = 0; i < table.size(); i++) {
    const StdBuiltin& b = table[i];
    if (name != b.name || argc != b.params.size() || argc != argKinds.size())
      continue;
    bool ok = true;
    int exact = 0;
    for (size_t p = 0; p < argc && ok; p++) {
      SBType a = argKinds[p], f = b.params[p];
      if (a == f) {
        exact++;
      } else if (f == SBType::Float && a == SBType::Int) {
        // promoção int → double
      } else if (f == SBType::Int && a == SBType::Bool) {
        exact++; // bool é i8/i64 na prática
      } else if (f == SBType::List && a == SBType::List) {
        exact++;
      } else if (f == SBType::Ptr && a == SBType::Int) {
        // Ptr (handle de 8 bytes) pode vir de um int literal (cast implícito
        // do codegen via Movq Imm em %rcx). Conta como match exato.
        exact++;
      } else if (f == SBType::Ptr && a == SBType::Str) {
        // FFI v2: string (char*) passada como ptr (ex.: nomes de extensão
        // Vulkan em arrays de char*). O valor char* vai by value.
        exact++;
      } else if (f == SBType::Int && a == SBType::Ptr) {
        // int retornado por mutex_new() usado como int é razoável (truncado)
        exact++;
      } else {
        ok = false;
      }
    }
    if (!ok) continue;
    if (exact > bestScore) {
      bestScore = exact;
      best = (int)i;
    }
  }
  return best;
}

} // namespace hphl
