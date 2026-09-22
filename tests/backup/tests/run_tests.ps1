# run_tests.ps1 — suite de testes do compilador HP-HL (Milestone 2)
#
# Uso:  powershell -ExecutionPolicy Bypass -File run_tests.ps1
# O script compila os exemplos, executa e compara a saída com o esperado.
# (compile o hphlc com `make` em compiler/ antes de rodar)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$compiler = Join-Path $root "compiler\hphlc.exe"
$examples = Join-Path $root "examples"
$tests_dir = $PSScriptRoot
function Get-TestPath($name) {
    $pos = Join-Path $tests_dir "positive/$name.hphl"
    $neg = Join-Path $tests_dir "negative/$name.hphl"
    if ($name.StartsWith("erro_") -and (Test-Path $neg)) { return $neg }
    if (Test-Path $pos) { return $pos }
    if (Test-Path $neg) { return $neg }
    return $null
}

$tmp = Join-Path $root "tests\_out"

if (-not (Test-Path $compiler)) {
    Write-Host "ERRO: compilador não encontrado em $compiler (rode 'make' em compiler/)" -ForegroundColor Red
    exit 1
}

# runtime.c é localizado via HPHL_EXE_DIR; saída do compilador/executável é UTF-8
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$env:HPHL_EXE_DIR = Join-Path $root "compiler"

New-Item -ItemType Directory -Force -Path $tmp | Out-Null

# nome, esperado (multiline)
$cases = @(
    @{
        name = "hello"
        expected = @"
Olá, mundo HP-HL!
x = 42
pi = 3.14159
5! = 120
soma 1..10 = 55
"@
    },
    @{
        name = "classes"
        expected = @"
global = 1000
contador = 7
nome = Ana
idade = 30
Ana!
quinta = 11
somar 1..100 = 5050
while n = 5
do m = 3
soma (skip 3, stop 7) = 18
case 11
f = 2
hx = 8
"@
    },
    @{
        name = "policies"
        expected = @"
stack = 11
heap = 22
arena = 33
pool = 44
shared = 55
texto na heap
f = 5
"@
    },
    @{
        name = "specialchars"
        expected = @"
ç ção ções
á é í ó ú â ê ô ã õ à ü
acentuação preservada
"@
    },
    @{
        name = "arrays"
        expected = @"
soma 1..5 = 15
g[2] = 30
nums[0] = 100
nums[1] = 3
length = 5
soma heap = 34
soma floats = 7
m[1][0] = 3
total m = 10
"@
    },
    @{
        name = "modules"
        expected = @"
double 21 = 42
baseFactor = 10
status = 0
point sum = 7
apply 5 = 25
"@
    },
    @{
        name = "lists"
        expected = @"
empty length: 0
scores[0] = 10
length = 3
scores[1] = 99
total = 139
player hp = 100
player hp = 80
after change, team[0].hp = 120
temps[0] = 36.5
words[1] = world
done
"@
    },
    @{
        name = "maps"
        expected = @"
empty length: 0
length = 3
ana = 30
caio = 41
has bia = true
has zeca = false
ana nova = 31
length ainda = 3
removed = true
length apos remove = 2
has bia agora = false
names[2] = dois
names length = 3
apos clear = 0
done
"@
    },
    @{
        name = "tuples"
        expected = @"
q = 3
r = 2
nome = ana
idade = 30
done
"@
    },
    @{
        name = "generics_where"
        expected = @"
5
3.75
olá mundo
42
ana
done
"@
    },
    @{
        name = "valuegen"
        expected = @"
4
8
4
8
30
7
done
"@
    },
    @{
        name = "visibility_members"
        expected = @"
50
50
60
"@
    },
    @{
        name = "specialize"
        expected = @"
64
64
"@
    },
    @{
        name = "compiletime"
        expected = @"
64
610
120
0
0
7
"@
    },
    @{
        name = "reflect_meta"
        expected = @"
24
32
1
1
0
24
"@
    },
    @{
        name = "policies_deep"
        expected = @"
-30
-10
-20
-5
10
"@
    },
    @{
        name = "move"
        expected = @"
10
99
"@
    }
    @{
        name = "move_cond"
        expected = @"
1
1
"@
    },
    @{
        name = "poly"
        expected = @"
100
8
sou enemysou enemy
8
done
"@
    },
    @{
        name = "overflow"
        expected = @"
b = -128
b = 127
w = -2147483648
c = 127
c = -128
s = 2147483647
k = 3000000
m = 300
p*p = 10000000000
done
"@
    },
    @{
        name = "visibility"
        expected = @"
open = 43
baseScore = 100
state = 1
bump = 6
open2 = 43
done
"@
    },
    @{
        name = "alias_reexport"
        expected = @"
counter = 7
renamed = 2
reexport = 2
enum alias = 1
via reexport fn = 11
done
"@
    },
    @{
        name = "match"
        expected = @"
a = slow
b = 10.0.0.1
c = off
bandwidth a = 100
bandwidth b = 10
different
ping positive: 5000
one
other
small
other
negative one
done
"@
    },
    @{
        name = "option"
        expected = @"
Some(other)
None
half 10 = 5
half 7 = -1
withDefault Some(3) = 3
withDefault None = 99
halfOrZero Ok(8) = 2
halfOrZero Ok(9) = 0
halfOrZero Err = 0
assert ok
none default applied
"@
    },
    @{
        name = "structs"
        expected = @"
7
10
20
10
10
99
30
10
70
57
47
"@
    },
    @{
        name = "patterns"
        expected = @"
10
7
-1
1
3
4
3
-1
100
7
500
200
"@
    },
    @{
        name = "listpat"
        expected = @"
3
21
hc=121
len2=2
hc2=70
pair=30
5
-1
2
done
"@
    },
    @{
        name = "erros"
        expected = @"
graviu ok
propagado: falhou
div ok: 5
div por zero: 5
42 x
7 y
"@
    },
    @{
        name = "trycatch"
        expected = @"
5
capturado: divisao por zero
1o catch: interno
2o catch: externo
fim
"@
    },
    @{
        name = "cond"
        expected = @"
-1
sem nome

110
ana
ana!
110
"@
    },
    @{
        name = "generics"
        expected = @"
caixa int: 99
caixa str: oi
primeiro: 7
infer: 10
soma: 5
"@
    },
    @{
        name = "refout"
        expected = "7816108111821421004141612"
    },
    @{
        name = "properties"
        expected = @"
vida inicial: 0
vida depois: 150
vida com valor negativo: 0
max: 100
nome vazio vira: sem nome
bônus (propriedade herdada): 40
temp: 100
"@
    },
    @{
        name = "match_expr"
        expected = @"
rapido
lento
outro
2
2
1
180
lento
"@
    },
    @{
        name = "concurrent"
        expected = @"
global = 4
global2 = 13
local = 61
saldo = 75
antigo = 7
novo = 9
protegido = 99
done
"@
    },
    @{
        name = "threads"
        expected = @"
somando = 90003
a = 1000
b = 2000
done
"@
    }
    @{
        name = "tasks"
        expected = @"
t1 = 200
t2 = 1004
ok
done
"@
    },
    @{
        name = "outdef"
        expected = "1516421577"
    }
    @{
        name = "parallel_cap"
        expected = @"
capturado = 600
local agora = 999
done
"@
    }
    @{
        name = "channel"
        expected = @"
marcador = -2
soma = 15
done
"@
    }
    @{
        name = "channel_sendable"
        expected = @"
42
"@
    }
    @{
        name = "spawn_sendable"
        expected = @"
42
1
"@
    }
    @{
        name = "null_analysis"
        expected = @"
-1
0
5
"@
    }
    @{
        name = "lock_discipline"
        expected = @"
7
8
99
"@
    }
    @{
        name = "arena_reset"
        expected = @"
10
20
"@
    }
    @{
        name = "std_math"
        expected = @"
7
7
2.5
-2
3
0.5
1.5
2
3
3
1.5
1024
3
3.14159
2.71828
"@
    }
    @{
        name = "std_string"
        expected = @"
22
Olá, Mundo HP-HL!|
18
Mundo
6
-1
true
true
true
ABC
abc
x|
"@
    }
    @{
        name = "std_file"
        expected = @"
true
16
24
true
false
0
"@
    }
    @{
        name = "std_misc"
        expected = @"
random ok
dado ok
env ok
sleep ok
"@
    }
    @{
        name = "async"
        expected = @"
a = 82
b = 6
contador = 4
done
"@
    }
    @{
        name = "threadlocal"
        expected = @"
soma das tarefas = 3000
slot da main = 0
done
"@
    }
    @{
        name = "parallel_foreach"
        expected = @"
soma arrays = 14400
soma listas = 5050
done
"@
    }
    @{
        name = "nested"
        expected = @"
soma = 20
spawns internos = 3
soma = 235
k local = 0
done
"@
    },
    @{
        name = "clock_builtin"
        expected = @"
clock ok 499500
"@
    }
    @{
        name = "cancel"
        expected = @"
0
42
42
7
done
"@
    }
    @{
        name = "deterministic"
        expected = @"
22
6
done
"@
    }
    @{
        name = "batch"
        expected = @"
soma batch = 210
soma list = 1900
done
"@
    }
    @{
        name = "actor"
        expected = @"
6
done
"@
    }
    @{
        name = "primitives"
        expected = @"
100
4
1
2
done
"@
    },
    @{
        name = "awaitchan"
        expected = @"
7
done
"@
    },
    @{
        name = "derives"
        expected = @"
true
false
true
true
false
true
true
true
true
done
"@
    }
)

$failed = 0
foreach ($c in $cases) {
    $src = Get-TestPath $c.name
    $exe = Join-Path $tmp ($c.name + ".exe")
    Write-Host "=== $($c.name) ===" -ForegroundColor Cyan
    cmd /c "`"$compiler`" `"$src`" -o `"$exe`"" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
        $failed++
        continue
    }
    $output = & $exe
    if ($LASTEXITCODE -ne 0) {
        Write-Host "  FALHA: execução retornou $LASTEXITCODE" -ForegroundColor Red
        $failed++
        continue
    }
    $actual = ($output -join "`n").TrimEnd("`r", "`n") + "`n"
    $expected = $c.expected.TrimEnd("`r", "`n") + "`n"
    if ($actual -eq $expected) {
        Write-Host "  OK" -ForegroundColor Green
    } else {
        Write-Host "  FALHA: saída difere do esperado" -ForegroundColor Red
        Write-Host "--- esperado ---" -ForegroundColor Yellow
        Write-Host $expected
        Write-Host "--- obtido ---" -ForegroundColor Yellow
        Write-Host $actual
        $failed++
    }
}

# teste negativo: erro semântico deve falhar com código != 0
Write-Host "=== erro_semantico ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_semantico.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro semântico, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: threadlocal só vale em global (M2); local é stack
Write-Host "=== erro_threadlocal ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_threadlocal.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de politica, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: threadlocal global não aceita inicializador (bloco zerado)
Write-Host "=== erro_threadlocal_init ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_threadlocal_init.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de inicializador em threadlocal, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: threadlocal de classe não é suportado (apenas escalares)
Write-Host "=== erro_threadlocal_class ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_threadlocal_class.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de tipo em threadlocal, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: parallel foreach exige sujeito compartilhado (array global
# ou list local); array local é blob da stack, invisível às tarefas
Write-Host "=== erro_parallel_foreach_sujeito ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_parallel_foreach_sujeito.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de sujeito, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: o corpo de parallel foreach não lê locais do escopo externo
Write-Host "=== erro_parallel_foreach_local ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_parallel_foreach_local.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de local externo, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: referência a variável de OUTRA parte do parallel (v0.22.8)
Write-Host "=== erro_parallel_sibling ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_parallel_sibling.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de variável de outra parte, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: semaphore sem contador inicial (v0.24.0)
Write-Host "=== erro_primitive_missing_arg ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_primitive_missing_arg.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de contador inicial, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: argumento em mutex/event (v0.24.0)
Write-Host "=== erro_primitive_extra_arg ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_primitive_extra_arg.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de argumento, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: primitiva como global (v0.24.0)
Write-Host "=== erro_primitive_global ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_primitive_global.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de global, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: índice fora dos limites deve panicar em runtime (código != 0)
Write-Host "=== erro_index ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_index.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro_index.exe`"" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
    $failed++
} else {
    cmd /c "`"$tmp\erro_index.exe`" 1>nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  FALHA: esperava panic de índice fora dos limites" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  OK (panic de bounds, código $LASTEXITCODE)" -ForegroundColor Green
    }
}

# teste negativo: módulo inexistente no import deve falhar na compilação
Write-Host "=== erro_import ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_import.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de import, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: índice fora dos limites de list deve panicar em runtime
Write-Host "=== erro_list_index ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_list_index.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro_list_index.exe`"" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
    $failed++
} else {
    cmd /c "`"$tmp\erro_list_index.exe`" 1>nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  FALHA: esperava panic de índice fora dos limites" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  OK (panic de bounds, código $LASTEXITCODE)" -ForegroundColor Green
    }
}

# teste negativo: overflow checked deve panicar em runtime
Write-Host "=== erro_overflow ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_overflow.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro_overflow.exe`"" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
    $failed++
} else {
    cmd /c "`"$tmp\erro_overflow.exe`" 1>nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  FALHA: esperava panic de overflow checked" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  OK (panic de overflow, código $LASTEXITCODE)" -ForegroundColor Green
    }
}

# teste negativo: membro internal não pode ser usado fora do módulo
Write-Host "=== erro_visibility ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_visibility.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de visibilidade, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# v0.46: negativos de private/protected em membros de classe
foreach ($vn in @("erro_private_field", "erro_private_method", "erro_protected_field",
                  "erro_private_inherited", "erro_private_property",
                  "erro_specialize_arity", "erro_specialize_body",
                  "erro_reflect_not_reflected", "erro_reflect_missing",
                  "erro_arena_escape", "erro_thread_race", "erro_use_after_move",
                  "erro_thread_race_inc", "erro_frame_escape",
                  "erro_channel_sendable", "erro_spawn_array_escape",
                  "erro_spawn_not_sendable", "erro_pool_escape",
                  "erro_use_after_move_branches", "erro_null_deref",
                  "erro_lock_toctou", "erro_arena_uaf",
                  "erro_std_builtin_bad")) {
    Write-Host "=== $vn ===" -ForegroundColor Cyan
    $bad = Join-Path $root "tests\$vn.hphl"
    cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  FALHA: esperava erro de visibilidade, compilou com sucesso" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
    }
}

# teste negativo: nome de tipo não qualificado definido em dois módulos é ambíguo
Write-Host "=== erro_ambig ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_ambig.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de ambiguidade, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: `depends on` módulo desconhecido deve falhar
Write-Host "=== erro_dep ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_dep.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de dependência desconhecida, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: ciclo de dependências deve falhar
Write-Host "=== erro_dep_cycle ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_dep_cycle.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de ciclo de dependências, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: match exaustivo de enum deve cobrir todas as variantes
Write-Host "=== erro_match_exhaust ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_exhaust.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de match não exaustivo, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: guarda 'when' de match deve ser bool
Write-Host "=== erro_match_guard ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_guard.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de guarda não-bool, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: braço de match duplicado (mesma tag sem guarda)
Write-Host "=== erro_match_dup ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_dup.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de padrão duplicado, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: número de bindings da variante deve bater com o payload
Write-Host "=== erro_match_binding ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_binding.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de quantidade de bindings, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Some/None/Ok/Err sem tipo esperado (contexto) deve falhar
Write-Host "=== erro_option_none_ctx ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_option_none_ctx.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de Option sem contexto, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: '?' exige função retornando Option/Result
Write-Host "=== erro_try_ret ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_try_ret.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '?' sem retorno Option/Result, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: '?' só se aplica a Option/Result (não a int)
Write-Host "=== erro_try_tipo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_try_tipo.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '?' sobre int, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: padrão Some(...) em sujeito inteiro deve falhar
Write-Host "=== erro_option_match ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_option_match.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de padrão Some em sujeito int, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: assert falso deve panicar em runtime (código != 0)
Write-Host "=== erro_panic ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_panic.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro_panic.exe`"" | Out-Null
if ($LASTEXITCODE -ne 0) {
    Write-Host "  FALHA: compilação retornou $LASTEXITCODE" -ForegroundColor Red
    $failed++
} else {
    cmd /c "`"$tmp\erro_panic.exe`" 1>nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  FALHA: esperava panic do assert, executou com sucesso" -ForegroundColor Red
        $failed++
    } else {
        Write-Host "  OK (panic em runtime, código $LASTEXITCODE)" -ForegroundColor Green
    }
}

# teste negativo: struct não pode herdar de classe
Write-Host "=== erro_struct_base ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_struct_base.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de struct com classe base, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: classe/struct sem método exigido pela interface
Write-Host "=== erro_iface_missing ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_iface_missing.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de método de interface ausente, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: interface usada como tipo de dado (sem instância possível) deve falhar
Write-Host "=== erro_iface_type ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_iface_type.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de interface como tipo de dado, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: interface não pode ter campo de dados
Write-Host "=== erro_iface_field ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_iface_field.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de campo em interface, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: método de interface não pode ter corpo
Write-Host "=== erro_iface_body ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_iface_body.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de corpo em método de interface, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: campo inexistente no padrão de struct
Write-Host "=== erro_struct_pat_field ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_struct_pat_field.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de campo inexistente no padrão, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: campo do padrão de struct deve ser inteiro
Write-Host "=== erro_struct_pat_float ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_struct_pat_float.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de campo não-inteiro no padrão, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: padrão de struct exige sujeito struct
Write-Host "=== erro_struct_pat_subject ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_struct_pat_subject.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de sujeito não-struct, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: traço derivado desconhecido
Write-Host "=== erro_derive_desconhecido ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_desconhecido.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de derive desconhecido, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Comparable exige enum de valores simples
Write-Host "=== erro_derive_comparable ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_comparable.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de Comparable com payload, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: derive em class/struct com traço invalido (v0.25.0)
Write-Host "=== erro_derive_struct ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_struct.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de derive em struct, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: derive Equatable em classe com campo list (v0.25.0)
Write-Host "=== erro_derive_list ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_list.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de campo list em derive Equatable, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: método customizado conflita com método gerado pelo derive (v0.25.0)
Write-Host "=== erro_derive_conflito ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_conflito.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de método conflitante com derive, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: derive exige declaração de enum
Write-Host "=== erro_derive_funcao ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_funcao.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de derive sem enum, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: derive não aceita pós-fixo (spec 37/38 — só prefixo)
Write-Host "=== erro_derive_posfixo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_derive_posfixo.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de derive pós-fixo, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: enum sem Comparable não pode usar <
Write-Host "=== erro_enum_ord ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_enum_ord.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de comparação de enum, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: FromInt exige inteiro
Write-Host "=== erro_fromint ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_fromint.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de FromInt com não-inteiro, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: `..resto` em array (tamanho fixo) deve falhar
Write-Host "=== erro_list_rest_array ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_list_rest_array.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '..resto' em array, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: contagem de elementos do padrão de array deve bater
Write-Host "=== erro_list_pat_count ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_list_pat_count.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de contagem de padrão de array, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: match sobre list sem braço '..'/'_'/'[]'+'..' 
Write-Host "=== erro_match_list_empty ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_list_empty.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de match de list não exaustivo, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: padrão de lista em sujeito inteiro falha
Write-Host "=== erro_list_pat_subject ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_list_pat_subject.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de padrão de lista em sujeito int, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: mais de um '..resto' no mesmo padrão falha
Write-Host "=== erro_rest_dup ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_rest_dup.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '..resto' duplicado, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Serialize() exige derive Serializable
Write-Host "=== erro_serialize ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_serialize.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de Serialize sem derive, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Ok() com payload em Result<void, E> falha
Write-Host "=== erro_result_void_ok_payload ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_result_void_ok_payload.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de payload ilegal em Result<void, E>, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Ok sem payload em Result<int, E> falha
Write-Host "=== erro_result_ok_sem_payload ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_result_ok_sem_payload.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de Ok sem payload, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: argumento nomeado com parâmetro inexistente falha
Write-Host "=== erro_named_arg_desconhecido ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_named_arg_desconhecido.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de parâmetro nomeado desconhecido, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: argumento nomeado faltante falha
Write-Host "=== erro_named_arg_faltando ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_named_arg_faltando.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de argumento nomeado faltante, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: misturar nomeado e posicional falha
Write-Host "=== erro_named_misto ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_named_misto.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de mistura nomeado/posicional, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: padrão Ok(v) em Result<void, E> falha
Write-Host "=== erro_result_pad_payload ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_result_pad_payload.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de padrão Ok com payload em Result<void, E>" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: '?.' em tipo não-classe falha
Write-Host "=== erro_optmember_nao_classe ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_optmember_nao_classe.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '?.' em não-classe, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: método inexistente após '?.' falha
Write-Host "=== erro_optmember_metodo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_optmember_metodo.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de método inexistente após '?.', compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: valor Option não cabe em alvo int sem '??'
Write-Host "=== erro_optmember_option ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_optmember_option.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de Option sem '??', compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: throw com tipo incompatível com o catch
Write-Host "=== erro_try_tipos ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_try_tipos.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de tipo do throw vs catch, compilou com sucesso" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: try sem catch falha no parser
Write-Host "=== erro_try_sem_catch ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_try_sem_catch.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de try sem catch, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, c�digo $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: argumento de tipo viola constraint 'class'
Write-Host "=== erro_generico_constraint ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_generico_constraint.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de constraint violada, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, c�digo $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: <...> em fun��o nao-gen�rica
Write-Host "=== erro_generico_nao_generica ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_generico_nao_generica.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de <...> em fun��o nao-gen�rica, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, c�digo $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: aridade errada de argumentos de tipo
Write-Host "=== erro_generico_arity ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_generico_arity.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de aridade de tipo, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, c�digo $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: parâmetro 'ref' exige lvalue no argumento
Write-Host "=== erro_ref_rvalue ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_ref_rvalue.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de ref com rvalue, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: parâmetro 'out' exige lvalue (argumento)
Write-Host "=== erro_out_rvalue ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_out_rvalue.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de out com rvalue, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: valor padrão não literal
Write-Host "=== erro_default_nao_literal ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_default_nao_literal.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de valor padrão não-literal, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: obrigatório depois de opcional
Write-Host "=== erro_default_posicao ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_default_posicao.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de obrigatório após opcional, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: ref/out/in com valor padrão
Write-Host "=== erro_ref_default ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_ref_default.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de ref com valor padrão, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: escrita em propriedade somente-leitura
Write-Host "=== erro_prop_readonly ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_prop_readonly.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de set em propriedade somente-leitura, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: leitura de propriedade somente-escrita
Write-Host "=== erro_prop_somente_escrita ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_prop_writeonly.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de leitura de propriedade somente-escrita, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: braços do match misturando '=> expr' e bloco '{ ... }'
Write-Host "=== erro_match_misto ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_misto.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de mistura bloco/expressao no match, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: braços '=>' com tipos incompatíveis
Write-Host "=== erro_match_tipos ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_match_tipos.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de tipos divergentes nos braços do match, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: atomic exige tipo inteiro
Write-Host "=== erro_atomic_float ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_atomic_float.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de 'atomic' com float, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: '*=' em atomic não é suportado
Write-Host "=== erro_atomic_mul ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_atomic_mul.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de '*=' em atomic, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: ref/out de variável atomic (endereço não pode escapar)
Write-Host "=== erro_atomic_ref ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_atomic_ref.hphl"
cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de 'out' de atomic, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: return dentro de lock (evita trava do spinlock)
Write-Host "=== erro_lock_return ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_lock_return.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de return dentro de lock, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: lock aninhado (spinlock global não é reentrante)
Write-Host "=== erro_lock_nested ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_lock_nested.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de lock aninhado, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: lock exige referência de classe
Write-Host "=== erro_lock_tipo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_lock_tipo.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de lock com tipo de valor, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: `parallel` não captura `this` (campos/métodos de instância
# seguem vetados no M2 — captura por valor vale para locais desde v0.22.1,
# em `parallel` e em `spawn`)
Write-Host "=== erro_parallel_this ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_parallel_this.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de captura de 'this' em parallel, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: Send com tipo incompatível com o elemento do channel
Write-Host "=== erro_channel_type ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_channel_type.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de tipo no Send, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: capacidade do channel deve ser literal inteiro > 0
Write-Host "=== erro_channel_capacity ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_channel_capacity.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de capacidade inválida, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: channel possui apenas Send(x) e Receive()
Write-Host "=== erro_channel_method ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_channel_method.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de método desconhecido no channel, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: await espera uma expressão do tipo task<T>
Write-Host "=== erro_await_tipo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_await_tipo.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de operando de await, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: async em método de classe não é suportado no M2
Write-Host "=== erro_async_method ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_async_method.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de async em método, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: parâmetro ref/out/in em função async não é suportado no M2
Write-Host "=== erro_async_ref ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_async_ref.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de ref/out/in em async, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: try/catch dentro de spawn não é suportado no M2
Write-Host "=== erro_spawn_try ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_spawn_try.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de try dentro de spawn, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: return dentro de spawn não faz sentido
Write-Host "=== erro_spawn_return ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_spawn_return.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de return dentro de spawn, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: leitura de 'out' antes de atribuição definida (SPEC §5.2)
Write-Host "=== erro_out_read_unassigned ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_out_read_unassigned.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de leitura de 'out' sem atribuição, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: leitura de 'out' dentro de laço sem atribuição anterior
Write-Host "=== erro_out_read_loop ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_out_read_loop.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de leitura de 'out' no laço, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo (M5): concatenação com tipo não escalar ao lado de string
Write-Host "=== erro_qol_concat_tipo ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_qol_concat_tipo.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de concat com list, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo (M5): toString exige exatamente 1 argumento
Write-Host "=== erro_qol_tostring_aridade ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_qol_tostring_aridade.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de aridade do toString, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo (M5): multi-decl com 'var' exige inicializador por nome
Write-Host "=== erro_qol_multivar_var ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_qol_multivar_var.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de 'var' sem inicializador, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo (M5): multi-decl em campos de struct não é suportado
Write-Host "=== erro_qol_campo_virgula ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_qol_campo_virgula.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de campo com vírgula, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: chave de map deve ser int/string/bool/char (M10.1)
Write-Host "=== erro_map_key ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_map_key.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de tipo de chave de map, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: map não possui inicializador
Write-Host "=== erro_map_init ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_map_init.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava erro de inicializador de map, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: string não suporta '-' (where T : supports -)
Write-Host "=== erro_where_supports ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_where_supports.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava violação de supports, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# teste negativo: classe não é sendable (where T : sendable)
Write-Host "=== erro_where_sendable ===" -ForegroundColor Cyan
$bad = Join-Path $root "tests\negative\erro_where_sendable.hphl"
& cmd /c "`"$compiler`" `"$bad`" -o `"$tmp\erro.exe`" 2>nul" | Out-Null
if ($LASTEXITCODE -eq 0) {
    Write-Host "  FALHA: esperava violação de sendable, compilou" -ForegroundColor Red
    $failed++
} else {
    Write-Host "  OK (erro detectado, código $LASTEXITCODE)" -ForegroundColor Green
}

# smoke test: lowering para HIR (--dump-hir) em todos os exemplos de sucesso
Write-Host ""
Write-Host "=== smoke test --dump-hir (exemplos) ===" -ForegroundColor Cyan
$dumpExamples = Get-ChildItem -Path (Join-Path $root "examples") -Filter "*.hphl" | Sort-Object Name
foreach ($ex in $dumpExamples) {
    & cmd /c "`"$compiler`" --dump-hir `"$($ex.FullName)`" >nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK $($ex.Name)" -ForegroundColor Green
    } else {
        Write-Host "  FALHA $($ex.Name) (exit $LASTEXITCODE)" -ForegroundColor Red
        $failed++
    }
}

# M10.3: smoke test do MIR (--dump-mir) em todos os exemplos
Write-Host ""
Write-Host "=== smoke test --dump-mir (exemplos) ===" -ForegroundColor Cyan
foreach ($ex in $dumpExamples) {
    & cmd /c "`"$compiler`" --dump-mir `"$($ex.FullName)`" >nul 2>nul"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  OK $($ex.Name)" -ForegroundColor Green
    } else {
        Write-Host "  FALHA $($ex.Name) (exit $LASTEXITCODE)" -ForegroundColor Red
        $failed++
    }
}

Write-Host ""
if ($failed -eq 0) {
    Write-Host "TODOS OS TESTES PASSARAM" -ForegroundColor Green
} else {
    Write-Host "$failed teste(s) falharam" -ForegroundColor Red
    exit 1
}
