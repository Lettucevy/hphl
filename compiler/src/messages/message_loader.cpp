#include "message_loader.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace hphl {

// M30 v0.89.0: Minimal fallback JSON parser for the small subset we need
// (flat object: "key": "value"). Avoids dependency on nlohmann/json.hpp.
// All values are stored as strings; .get() returns them as-is.
static std::string extractString(const std::string& s, size_t& pos) {
    // skip whitespace
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
    if (pos >= s.size() || s[pos] != '"') return "";
    pos++;
    std::string out;
    while (pos < s.size() && s[pos] != '"') {
        if (s[pos] == '\\' && pos + 1 < s.size()) {
            char c = s[++pos];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: out += c; break;
            }
        } else {
            out += s[pos];
        }
        pos++;
    }
    if (pos < s.size()) pos++;  // closing "
    return out;
}

#if defined(_WIN32)
#include <windows.h>
static std::string getExeDir() {
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        std::string p(buf, len);
        size_t pos = p.find_last_of("\\/");
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return "";
}
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
static std::string getExeDir() {
    char buf[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len > 0) {
        buf[len] = '\0';
        std::string p(buf);
        size_t pos = p.find_last_of('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return "";
}
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
static std::string getExeDir() {
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::string p(buf);
        size_t pos = p.find_last_of('/');
        if (pos != std::string::npos) return p.substr(0, pos);
    }
    return "";
}
#else
static std::string getExeDir() { return ""; }
#endif

static std::string normalizeLocale(const std::string& raw) {
    std::string s = raw;
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)::tolower(c); });
    if (s.find("pt") != std::string::npos) return "ptbr";
    if (s.find("es") != std::string::npos) return "es";
    return "en";
}

// In-memory fallbacks for critical error keys so translation always works
// even if external JSON files are missing on disk.
static const std::map<std::string, std::map<std::string, std::string>> kEmbeddedDefaults = {
    {"en", {
        {"syntax_error_prefix", "syntax error"},
        {"parser_expected", "expected {0}"},
        {"parser_expected_after", "expected '{0}' after {1}"},
        {"parser_unexpected_token", "unexpected token '{0}'"},
        {"parser_unclosed_type_param", "'>' expected to close type parameters"},
        {"parser_invalid_return_type", "invalid return type"},
        {"parser_unclosed_paren", "')' expected"},
        {"parser_unclosed_brace", "'}' expected"},
        {"parser_unclosed_bracket", "']' expected"},
        {"semantic_error_prefix", "semantic error"},
        {"sem_var_already_declared", "variable '{0}' already declared in this scope"},
        {"sem_var_not_found", "variable '{0}' not declared"},
        {"sem_type_mismatch", "type mismatch: expected '{0}', but got '{1}'"},
        {"sem_class_duplicate", "duplicate class '{0}'"},
        {"sem_class_not_found", "class '{0}' not found"},
        {"sem_struct_duplicate", "duplicate struct '{0}'"},
        {"sem_enum_duplicate", "duplicate enum '{0}'"},
        {"sem_function_duplicate", "duplicate function '{0}'"},
        {"sem_method_not_found", "method '{0}' not found in class '{1}'"},
        {"sem_field_not_found", "field '{0}' not found in type '{1}'"},
        {"sem_arg_count_mismatch", "function '{0}' expects {1} arguments, but received {2}"},
        {"sem_module_not_found", "module '{0}' not found"},
        {"sem_dep_cycle", "dependency cycle: {0}"},
        {"sem_invalid_cast", "invalid conversion from '{0}' to '{1}'"},
        {"sem_cannot_assign", "cannot assign to '{0}'"},
        {"sem_not_callable", "expression of type '{0}' is not callable"},
        {"sem_iface_missing_method", "class '{0}' does not implement method '{1}' of interface '{2}'"},
        {"sem_break_outside_loop", "'break' outside loop"},
        {"sem_continue_outside_loop", "'continue' outside loop"},
        {"sem_return_outside_func", "'return' outside function"},
        {"sem_return_value_mismatch", "function expects return type '{0}', but returned '{1}'"},
        {"sem_void_return_val", "void function cannot return a value"},
        {"cli_compiling", "compiling '{0}'..."},
        {"cli_compiling_debug", "compiling '{0}' with debug instrumentation..."},
        {"cli_assembly_generated", "assembly generated at '{0}'"},
        {"cli_exe_generated", "executable generated at '{0}'"},
        {"cli_executing", "executing '{0}'..."},
        {"cli_build_success", "build completed successfully"},
        {"cli_link_failed", "failed to assemble/link (gcc returned {0})"},
        {"cli_no_entry_point", "no entry point 'Main' found (the file appears to be a library/module, not a program)"},
        {"cli_fetch_failed", "fetch failed: {0}"},
        {"cli_fetch_ok", "fetch OK: {0} (v{1})"},
        {"cli_install_failed", "install failed: {0}"},
        {"cli_install_ok", "install OK: {0} packages in package.lock"},
        {"cli_lockfile_fail", "could not write package.lock"},
        {"cli_target_os_changed", "Target OS changed to: {0}"},
        {"cli_backend_changed", "Backend changed to: {0}"},
        {"cli_cannot_read_file", "could not read file '{0}'"},
        {"cli_cannot_write_file", "could not write '{0}'"},
        {"cli_no_sources", "no sources to compile"},
        {"cli_dep_not_found", "dependency not found: '{0}'"},
        {"cli_published", "published {0} v{1} at '{2}'"},
        {"cli_publish_failed", "publish failed: {0}"},
        {"cli_package_info", "package {0} v{1}"},
        {"cli_llvm_ir_generated", "LLVM IR generated at '{0}'"},
        {"cli_llvm_obj_generated", "object file generated via LLVM API at '{0}' (-O{1})"},
        {"cli_wasm_generated", "WASM module generated at '{0}' (-O{1})"},
        {"cli_wasm_link_failed", "failed to link WASM (wasm-ld returned {0})"},
        {"cli_fetch_requires_arg", "fetch requires <package>[@<version>]"},
        {"cli_out_requires_dir", "--out requires a directory"},
        {"cli_precompile_runtime_failed", "warning: failed to precompile {0}; linking directly with runtime.c"},
        {"cli_empty_or_unreadable_source", "empty or unreadable source: {0}"},
        {"cli_assemble_failed", "failed to assemble {0}"},
        {"cli_task_must_be_object", "task '{0}' must be an object"},
        {"cli_task_sources_non_empty", "task '{0}': 'sources' must be a non-empty array"},
        {"cli_project_sources_non_empty", "project: 'sources' must be a non-empty array"},
        {"cli_task_depends_missing", "task '{0}' depends on '{1}' (not found)"},
        {"cli_task_not_found", "task '{0}' not found"},
        {"cli_cyclic_dependencies", "cyclic dependencies between tasks"},
        {"cli_no_objects_to_link", "no objects to link"},
        {"cli_linker_returned_code", "linker returned {0}"},
        {"loader_import_file_not_found", "import file not found: '{0}' (searched in '{1}')"},
        {"loader_module_not_found", "module '{0}' not found (searched in{1})"},
        {"loader_cyclic_import", "cyclic import involving '{0}'"},
        {"loader_imported_file_no_module", "imported file '{0}' does not declare `module Name;`"},
        {"loader_module_duplicate_files", "module '{0}' declared in two files ('{1}' and '{2}')"},
        {"cli_cannot_read_project", "could not read project '{0}'"},
        {"cli_project_invalid_json", "project invalid JSON: {0}"},
        {"cli_project_must_be_object", "project must be a JSON object"},
        {"cli_project_no_tasks", "project: no tasks found"},
        {"cli_project_source_not_found", "project: source not found '{0}'"},
        {"cli_project_no_objects_generated", "project: no object files generated (missing Main?)"},
        {"cli_task_up_to_date", "task '{0}' is up to date (output is newer)"},
        {"cli_project_up_to_date", "project '{0}' is up to date ({1} is newer than sources)"},
        {"cli_task_skipped", "task '{0}' skipped (when: {1})"},
        {"cli_task_error", "task '{0}': {1}"},
        {"cli_task_failed", "task '{0}' failed (rc={1})"},
        {"cli_task_ok", "task '{0}' OK ({1} objs)"},
        {"cli_project_link_error", "link '{0}' -> '{1}': {2}"},
        {"cli_project_built", "project '{0}' -> '{1}' ({2} objects, {3} tasks, jobs={4})"},
        {"parser_found", "found '{0}'"},
        {"warning_prefix", "warning"}
    }},
    {"ptbr", {
        {"syntax_error_prefix", "erro de sintaxe"},
        {"parser_expected", "esperava {0}"},
        {"parser_expected_after", "esperava '{0}' após {1}"},
        {"parser_unexpected_token", "token inesperado '{0}'"},
        {"parser_unclosed_type_param", "'>' esperado para fechar parâmetros de tipo"},
        {"parser_invalid_return_type", "tipo de retorno inválido"},
        {"parser_unclosed_paren", "')' esperado"},
        {"parser_unclosed_brace", "'}' esperado"},
        {"parser_unclosed_bracket", "']' esperado"},
        {"semantic_error_prefix", "erro semântico"},
        {"sem_var_already_declared", "variável '{0}' já declarada neste escopo"},
        {"sem_var_not_found", "variável '{0}' não declarada"},
        {"sem_type_mismatch", "tipo incompatível: esperado '{0}', mas recebeu '{1}'"},
        {"sem_class_duplicate", "classe '{0}' duplicada"},
        {"sem_class_not_found", "classe '{0}' não encontrada"},
        {"sem_struct_duplicate", "struct '{0}' duplicada"},
        {"sem_enum_duplicate", "enum '{0}' duplicado"},
        {"sem_function_duplicate", "função '{0}' duplicada"},
        {"sem_method_not_found", "método '{0}' não encontrado na classe '{1}'"},
        {"sem_field_not_found", "campo '{0}' não encontrado no tipo '{1}'"},
        {"sem_arg_count_mismatch", "função '{0}' espera {1} argumentos, mas recebeu {2}"},
        {"sem_module_not_found", "módulo '{0}' não encontrado"},
        {"sem_dep_cycle", "ciclo de dependência: {0}"},
        {"sem_invalid_cast", "conversão inválida de '{0}' para '{1}'"},
        {"sem_cannot_assign", "não é possível atribuir a '{0}'"},
        {"sem_not_callable", "expressão do tipo '{0}' não é invocável"},
        {"sem_iface_missing_method", "classe '{0}' não implementa o método '{1}' da interface '{2}'"},
        {"sem_break_outside_loop", "'break' fora de laço de repetição"},
        {"sem_continue_outside_loop", "'continue' fora de laço de repetição"},
        {"sem_return_outside_func", "'return' fora de função"},
        {"sem_return_value_mismatch", "função espera retorno do tipo '{0}', mas retornou '{1}'"},
        {"sem_void_return_val", "função void não deve retornar valor"},
        {"cli_compiling", "compilando '{0}'..."},
        {"cli_compiling_debug", "compilando '{0}' com instrumentação de debug..."},
        {"cli_assembly_generated", "assembly gerado em '{0}'"},
        {"cli_exe_generated", "executável gerado em '{0}'"},
        {"cli_executing", "executando '{0}'..."},
        {"cli_build_success", "compilação concluída com sucesso"},
        {"cli_link_failed", "falha ao montar/linkar (gcc retornou {0})"},
        {"cli_no_entry_point", "nenhum ponto de entrada 'Main' encontrado (o arquivo parece ser uma biblioteca/módulo e não um programa)"},
        {"cli_fetch_failed", "fetch falhou: {0}"},
        {"cli_fetch_ok", "fetch OK: {0} (v{1})"},
        {"cli_install_failed", "install falhou: {0}"},
        {"cli_install_ok", "install OK: {0} pacotes em package.lock"},
        {"cli_lockfile_fail", "não foi possível gravar package.lock"},
        {"cli_target_os_changed", "Target OS alterado para: {0}"},
        {"cli_backend_changed", "Backend alterado para: {0}"},
        {"cli_cannot_read_file", "não foi possível ler o arquivo '{0}'"},
        {"cli_cannot_write_file", "não foi possível gravar '{0}'"},
        {"cli_no_sources", "sem fontes para compilar"},
        {"cli_dep_not_found", "dependência não encontrada: '{0}'"},
        {"cli_published", "publicado {0} v{1} em '{2}'"},
        {"cli_publish_failed", "publish falhou: {0}"},
        {"cli_package_info", "pacote {0} v{1}"},
        {"cli_llvm_ir_generated", "LLVM IR gerado em '{0}'"},
        {"cli_llvm_obj_generated", "object file gerado via LLVM API em '{0}' (-O{1})"},
        {"cli_wasm_generated", "módulo WASM gerado em '{0}' (-O{1})"},
        {"cli_wasm_link_failed", "falha ao linkar WASM (wasm-ld retornou {0})"},
        {"cli_fetch_requires_arg", "fetch requer <pacote>[@<versao>]"},
        {"cli_out_requires_dir", "--out requer um diretório"},
        {"cli_precompile_runtime_failed", "aviso: falha ao pré-compilar {0}; linkando com o runtime.c direto"},
        {"cli_empty_or_unreadable_source", "fonte vazia ou ilegível: {0}"},
        {"cli_assemble_failed", "falha ao montar {0}"},
        {"cli_task_must_be_object", "task '{0}' deve ser um objeto"},
        {"cli_task_sources_non_empty", "task '{0}': 'sources' deve ser um array não vazio"},
        {"cli_project_sources_non_empty", "projeto: 'sources' deve ser um array não vazio"},
        {"cli_task_depends_missing", "task '{0}' depende de '{1}' (inexistente)"},
        {"cli_task_not_found", "task '{0}' inexistente"},
        {"cli_cyclic_dependencies", "dependências cíclicas entre tasks"},
        {"cli_no_objects_to_link", "nenhum objeto para linkar"},
        {"cli_linker_returned_code", "linker retornou {0}"},
        {"loader_import_file_not_found", "arquivo de import não encontrado: '{0}' (procurado em '{1}')"},
        {"loader_module_not_found", "módulo '{0}' não encontrado (procurado em{1})"},
        {"loader_cyclic_import", "import cíclico envolvendo '{0}'"},
        {"loader_imported_file_no_module", "arquivo importado '{0}' não declara `module Nome;`"},
        {"loader_module_duplicate_files", "módulo '{0}' declarado em dois arquivos ('{1}' e '{2}')"},
        {"cli_cannot_read_project", "não foi possível ler o projeto '{0}'"},
        {"cli_project_invalid_json", "projeto JSON inválido: {0}"},
        {"cli_project_must_be_object", "projeto deve ser um objeto JSON"},
        {"cli_project_no_tasks", "projeto: nenhuma task encontrada"},
        {"cli_project_source_not_found", "projeto: fonte não encontrada '{0}'"},
        {"cli_project_no_objects_generated", "projeto: nenhum objeto gerado (sem Main?)"},
        {"cli_task_up_to_date", "task '{0}' atualizada (output mais recente)"},
        {"cli_project_up_to_date", "projeto '{0}' atualizado ({1} mais recente que fontes)"},
        {"cli_task_skipped", "task '{0}' pulada (when: {1})"},
        {"cli_task_error", "task '{0}': {1}"},
        {"cli_task_failed", "task '{0}' falhou (rc={1})"},
        {"cli_task_ok", "task '{0}' OK ({1} objs)"},
        {"cli_project_link_error", "link '{0}' -> '{1}': {2}"},
        {"cli_project_built", "projeto '{0}' -> '{1}' ({2} objetos, {3} tasks, jobs={4})"},
        {"parser_found", "encontrado '{0}'"},
        {"warning_prefix", "aviso"}
    }},
    {"es", {
        {"syntax_error_prefix", "error de sintaxis"},
        {"parser_expected", "se esperaba {0}"},
        {"parser_expected_after", "se esperaba '{0}' después de {1}"},
        {"parser_unexpected_token", "token inesperado '{0}'"},
        {"parser_unclosed_type_param", "se esperaba '>' para cerrar parámetros de tipo"},
        {"parser_invalid_return_type", "tipo de retorno no válido"},
        {"parser_unclosed_paren", "se esperaba ')'"},
        {"parser_unclosed_brace", "se esperaba '}'"},
        {"parser_unclosed_bracket", "se esperaba ']'"},
        {"semantic_error_prefix", "error semántico"},
        {"sem_var_already_declared", "variable '{0}' ya declarada en este ámbito"},
        {"sem_var_not_found", "variable '{0}' no declarada"},
        {"sem_type_mismatch", "tipo incompatible: se esperaba '{0}', pero se recibió '{1}'"},
        {"sem_class_duplicate", "clase '{0}' duplicada"},
        {"sem_class_not_found", "clase '{0}' no encontrada"},
        {"sem_struct_duplicate", "struct '{0}' duplicada"},
        {"sem_enum_duplicate", "enum '{0}' duplicado"},
        {"sem_function_duplicate", "función '{0}' duplicada"},
        {"sem_method_not_found", "método '{0}' no encontrado en la clase '{1}'"},
        {"sem_field_not_found", "campo '{0}' no encontrado en el tipo '{1}'"},
        {"sem_arg_count_mismatch", "la función '{0}' espera {1} argumentos, pero recibió {2}"},
        {"sem_module_not_found", "módulo '{0}' no encontrado"},
        {"sem_dep_cycle", "ciclo de dependencias: {0}"},
        {"sem_invalid_cast", "conversión no válida de '{0}' a '{1}'"},
        {"sem_cannot_assign", "no se puede asignar a '{0}'"},
        {"sem_not_callable", "la expresión de tipo '{0}' no se puede invocar"},
        {"sem_iface_missing_method", "la clase '{0}' no implementa el método '{1}' de la interfaz '{2}'"},
        {"sem_break_outside_loop", "'break' fuera de bucle"},
        {"sem_continue_outside_loop", "'continue' fuera de bucle"},
        {"sem_return_outside_func", "'return' fuera de función"},
        {"sem_return_value_mismatch", "la función espera un retorno de tipo '{0}', pero retornó '{1}'"},
        {"sem_void_return_val", "la función void no debe retornar un valor"},
        {"cli_compiling", "compilando '{0}'..."},
        {"cli_compiling_debug", "compilando '{0}' con instrumentación de depuración..."},
        {"cli_assembly_generated", "ensamblado generado en '{0}'"},
        {"cli_exe_generated", "ejecutable generado en '{0}'"},
        {"cli_executing", "ejecutando '{0}'..."},
        {"cli_build_success", "compilación completada con éxito"},
        {"cli_link_failed", "fallo al ensamblar/enlazar (gcc devolvió {0})"},
        {"cli_no_entry_point", "ningún punto de entrada 'Main' encontrado (el archivo parece ser una biblioteca/módulo y no un programa)"},
        {"cli_fetch_failed", "la descarga falló: {0}"},
        {"cli_fetch_ok", "descarga OK: {0} (v{1})"},
        {"cli_install_failed", "la instalación falló: {0}"},
        {"cli_install_ok", "instalación OK: {0} paquetes en package.lock"},
        {"cli_lockfile_fail", "no se pudo escribir package.lock"},
        {"cli_target_os_changed", "Target OS cambiado a: {0}"},
        {"cli_backend_changed", "Backend cambiado a: {0}"},
        {"cli_cannot_read_file", "no se pudo leer el archivo '{0}'"},
        {"cli_cannot_write_file", "no se pudo escribir '{0}'"},
        {"cli_no_sources", "no hay fuentes para compilar"},
        {"cli_dep_not_found", "dependencia no encontrada: '{0}'"},
        {"cli_published", "publicado {0} v{1} en '{2}'"},
        {"cli_publish_failed", "la publicación falló: {0}"},
        {"cli_package_info", "paquete {0} v{1}"},
        {"cli_llvm_ir_generated", "LLVM IR generado en '{0}'"},
        {"cli_llvm_obj_generated", "archivo objeto generado mediante LLVM API en '{0}' (-O{1})"},
        {"cli_wasm_generated", "módulo WASM generado en '{0}' (-O{1})"},
        {"cli_wasm_link_failed", "fallo al enlazar WASM (wasm-ld devolvió {0})"},
        {"cli_fetch_requires_arg", "fetch requiere <paquete>[@<version>]"},
        {"cli_out_requires_dir", "--out requiere un directorio"},
        {"cli_precompile_runtime_failed", "aviso: fallo al precompilar {0}; enlazando directamente con runtime.c"},
        {"cli_empty_or_unreadable_source", "fuente vacía o ilegible: {0}"},
        {"cli_assemble_failed", "fallo al ensamblar {0}"},
        {"cli_task_must_be_object", "la tarea '{0}' debe ser un objeto"},
        {"cli_task_sources_non_empty", "tarea '{0}': 'sources' debe ser un array no vacío"},
        {"cli_project_sources_non_empty", "proyecto: 'sources' debe ser un array no vacío"},
        {"cli_task_depends_missing", "la tarea '{0}' depende de '{1}' (inexistente)"},
        {"cli_task_not_found", "tarea '{0}' no encontrada"},
        {"cli_cyclic_dependencies", "dependencias cíclicas entre tareas"},
        {"cli_no_objects_to_link", "ningún objeto para enlazar"},
        {"cli_linker_returned_code", "el enlazador devolvió {0}"},
        {"loader_import_file_not_found", "archivo de importación no encontrado: '{0}' (buscado en '{1}')"},
        {"loader_module_not_found", "módulo '{0}' no encontrado (buscado en{1})"},
        {"loader_cyclic_import", "importación cíclica que involucra a '{0}'"},
        {"loader_imported_file_no_module", "el archivo importado '{0}' no declara `module Nombre;`"},
        {"loader_module_duplicate_files", "módulo '{0}' declarado en dos archivos ('{1}' y '{2}')"},
        {"cli_cannot_read_project", "no se pudo leer el proyecto '{0}'"},
        {"cli_project_invalid_json", "JSON de proyecto no válido: {0}"},
        {"cli_project_must_be_object", "el proyecto debe ser un objeto JSON"},
        {"cli_project_no_tasks", "proyecto: no se encontraron tareas"},
        {"cli_project_source_not_found", "proyecto: fuente no encontrada '{0}'"},
        {"cli_project_no_objects_generated", "proyecto: ningún objeto generado (¿falta Main?)"},
        {"cli_task_up_to_date", "tarea '{0}' actualizada (salida más reciente)"},
        {"cli_project_up_to_date", "proyecto '{0}' actualizado ({1} más reciente que fuentes)"},
        {"cli_task_skipped", "tarea '{0}' omitida (when: {1})"},
        {"cli_task_error", "tarea '{0}': {1}"},
        {"cli_task_failed", "tarea '{0}' falló (rc={1})"},
        {"cli_task_ok", "tarea '{0}' OK ({1} objs)"},
        {"cli_project_link_error", "enlace '{0}' -> '{1}': {2}"},
        {"cli_project_built", "proyecto '{0}' -> '{1}' ({2} objetos, {3} tareas, jobs={4})"},
        {"parser_found", "encontrado '{0}'"},
        {"warning_prefix", "aviso"}
    }}
};

MessageLoader::MessageLoader() : locale_("en") {
    // English is the default language as requested.
    loadDefault();
}

bool MessageLoader::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::string exeDir = getExeDir();
        if (!exeDir.empty()) {
            std::string filename = path;
            size_t slash = filename.find_last_of("/\\");
            if (slash != std::string::npos) filename = filename.substr(slash + 1);

            std::string candidates[] = {
                exeDir + "/" + path,
                exeDir + "/../" + path,
                exeDir + "/src/" + path,
                exeDir + "/../src/" + path,
                exeDir + "/../../src/" + path,
                exeDir + "/messages/" + filename,
                exeDir + "/src/messages/" + filename,
                exeDir + "/../messages/" + filename,
                exeDir + "/../src/messages/" + filename,
                exeDir + "/../../compiler/src/messages/" + filename,
                exeDir + "/../compiler/src/messages/" + filename
            };
            for (auto& c : candidates) {
                file.open(c);
                if (file.is_open()) break;
            }
        }
    }
    if (!file.is_open()) {
        return false;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    std::string s = ss.str();
    size_t pos = 0;
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
    if (pos >= s.size() || s[pos] != '{') return false;
    pos++;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == ',')) pos++;
        if (pos >= s.size()) break;
        if (s[pos] == '}') break;
        std::string key = extractString(s, pos);
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
        if (pos < s.size() && s[pos] == ':') pos++;
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\n' || s[pos] == '\r')) pos++;
        std::string val = extractString(s, pos);
        messages_[key] = val;
    }
    return true;
}

bool MessageLoader::loadDefault() {
    return load("messages/en.json");
}

std::string MessageLoader::get(const std::string& key) const {
    auto it = messages_.find(key);
    if (it != messages_.end()) {
        return it->second;
    }
    std::string norm = normalizeLocale(locale_);
    auto locIt = kEmbeddedDefaults.find(norm);
    if (locIt != kEmbeddedDefaults.end()) {
        auto msgIt = locIt->second.find(key);
        if (msgIt != locIt->second.end()) return msgIt->second;
    }
    // Fallback to English embedded defaults
    if (norm != "en") {
        auto enIt = kEmbeddedDefaults.find("en");
        if (enIt != kEmbeddedDefaults.end()) {
            auto msgIt = enIt->second.find(key);
            if (msgIt != enIt->second.end()) return msgIt->second;
        }
    }
    return key;
}

std::string MessageLoader::get(const std::string& key, const std::vector<std::string>& params) const {
    std::string msg = get(key);
    for (size_t i = 0; i < params.size(); ++i) {
        std::string placeholder = "{" + std::to_string(i) + "}";
        size_t pos = msg.find(placeholder);
        while (pos != std::string::npos) {
            msg.replace(pos, placeholder.length(), params[i]);
            pos = msg.find(placeholder, pos + params[i].length());
        }
    }
    return msg;
}

void MessageLoader::setLocale(const std::string& locale) {
    std::string norm = normalizeLocale(locale);
    locale_ = norm;
    messages_.clear();
    load("messages/" + norm + ".json");
}

std::string MessageLoader::getLocale() const {
    return locale_;
}

bool MessageLoader::loadLocaleMessages() {
    return load("messages/" + normalizeLocale(locale_) + ".json");
}

std::string MessageLoader::substituteParams(const std::string& msg,
                                             const std::vector<std::string>& params) const {
    std::string result = msg;
    for (size_t i = 0; i < params.size(); ++i) {
        std::string placeholder = "{" + std::to_string(i) + "}";
        size_t pos = result.find(placeholder);
        while (pos != std::string::npos) {
            result.replace(pos, placeholder.length(), params[i]);
            pos = result.find(placeholder, pos + params[i].length());
        }
    }
    return result;
}

} // namespace hphl
