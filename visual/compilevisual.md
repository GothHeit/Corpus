# Compilando o `visual/`

Comando básico:

```sh
g++ test.cpp window.cpp ../src/library.cpp ../src/file.cpp ../src/tag.cpp ../src/saving.cpp ../src/tag_query.cpp -o visual -ldwmapi -lgdi32 -lgdiplus -luser32
```

## DLL faltando ao rodar em outra máquina

Se o `.exe` reclamar de DLL faltando ao rodar em outra máquina (ex.: `libgcc_s_seh-1.dll not found`,
`libstdc++-6.dll not found`), é porque por padrão o `g++` linka o runtime do MinGW (`libgcc`, `libstdc++`)
como DLL, exigindo que o MinGW esteja instalado (ou as DLLs presentes) na máquina que vai **rodar** o
programa — não só na que compilou. Pra evitar isso, é possível embutir o runtime dentro do próprio `.exe`:

```sh
g++ test.cpp window.cpp ../src/library.cpp ../src/file.cpp ../src/tag.cpp ../src/saving.cpp ../src/tag_query.cpp -o visual -ldwmapi -lgdi32 -lgdiplus -luser32 -static-libgcc -static-libstdc++ -static
```

| Flag | Efeito |
|---|---|
| `-static-libgcc` | embute o runtime do gcc (`libgcc`) no `.exe` em vez de depender de `libgcc_s_seh-1.dll` |
| `-static-libstdc++` | embute a std lib (`libstdc++`) no `.exe` em vez de depender de `libstdc++-6.dll` |
| `-static` | idem para as demais libs que o g++/MinGW linkaria dinamicamente (ex.: `libwinpthread`) |

O `.exe` fica maior (todo o runtime vai junto), mas roda sozinho em qualquer Windows sem precisar
instalar nada. Sem essas flags, funciona igual numa máquina que já tenha o MinGW instalado.
