# Proyecto de Sistemas Operativos

## Proyecto 3: Tablas de páginas en acción (xv6)

### Integrantes
- Daniela Giraldo Salas
- Matías Gil Montoya
- Gisel Jaramillo Carmona

### Curso
Sistemas Operativos

### Semestre
2026-2

---

# Descripción

Extendimos xv6-riscv (commit `06aad25`) con tres mecanismos que usan la tabla de páginas de cada proceso.

**Parte A, página compartida y `ugetpid()`.** `USYSCALL` (`TRAPFRAME - PGSIZE`) es una página que el kernel mapea en todo proceso con `PTE_R | PTE_U`. Contiene `struct usyscall { int pid; }`. `ugetpid()` lee el pid de esa dirección y no hace ninguna llamada al sistema. La página se reserva en `allocproc()`, se mapea en `proc_pagetable()` y se libera en `freeproc()`. Los límites del heap (`growproc()` y `sys_sbrk()`) ahora se comparan contra `USYSCALL`.

**Parte B, `vmprint()`.** Llamada al sistema que imprime la tabla de páginas del proceso con un recorrido recursivo de los tres niveles Sv39, al estilo de `freewalk()`. Solo imprime PTEs válidas.

**Parte C, `pgaccess()`.** Define `PTE_A` (bit 6). Para `npages` páginas desde `base` arma una máscara con las que fueron leídas o escritas, la copia al usuario con `copyout()` y limpia `PTE_A` de las páginas reportadas. Las páginas sin memoria física se reportan como 0 y no se les asigna memoria.

---

# Archivos modificados

**kernel/memlayout.h**: define `USYSCALL` y `struct usyscall` (dentro de `#ifndef __ASSEMBLER__`, porque el archivo también lo incluye código en ensamblador).

**kernel/riscv.h**: agrega `PTE_A` (`1L << 6`).

**kernel/proc.h**: agrega el campo `struct usyscall *usyscall` a `struct proc`.

**kernel/proc.c**:
- `allocproc()` reserva la página con `kalloc()`, la pone en cero y guarda el pid.
- `proc_pagetable()` la mapea en `USYSCALL` con `PTE_R | PTE_U` y deshace los mapeos anteriores si falla.
- `proc_freepagetable()` desmapea `USYSCALL` sin liberarla, porque la física sigue siendo del proceso (también después de `exec()`).
- `freeproc()` libera la página física.
- `growproc()` compara contra `USYSCALL`.

**kernel/sysproc.c**: `sys_sbrk()` compara contra `USYSCALL`. Agrega `sys_vmprint()` y `sys_pgaccess()`.

**kernel/vm.c**: agrega `vmprint()` y su auxiliar recursiva `vmprintwalk()`.

**kernel/defs.h**: declara `vmprint()`.

**kernel/syscall.h, kernel/syscall.c**: registran `SYS_vmprint` (23) y `SYS_pgaccess` (24).

**user/usys.pl, user/user.h**: entradas y prototipos de `vmprint()`, `pgaccess()` y `ugetpid()`.

**user/ulib.c**: implementa `ugetpid()`.

---

# Integración con xv6

1. Clonar xv6-riscv y hacer `git switch -c proyecto3 06aad25`.
2. Copiar los archivos entregados en sus rutas.
3. Agregar `$U/_pgtbltest` a UPROGS en el Makefile.
4. Compilar con `make qemu`.
5. Ejecutar `pgtbltest` y `usertests -q`.

(`user/pgtbltest.c` y la versión del curso de `user/usertests.c` los entrega el docente.)

---

# Respuestas a las preguntas

## 1. Interpretación de vmprint()

Salida de un proceso recién creado (los valores cambian en cada ejecución):

```
page table 0x0000000087f22000
 ..0x0000000000000000: pte 0x0000000021fc7801 pa 0x0000000087f1e000
 .. ..0x0000000000000000: pte 0x0000000021fc7401 pa 0x0000000087f1d000
 .. .. ..0x0000000000000000: pte 0x0000000021fc7c5b pa 0x0000000087f1f000
 .. .. ..0x0000000000001000: pte 0x0000000021fc7017 pa 0x0000000087f1c000
 .. .. ..0x0000000000002000: pte 0x0000000021fc6c07 pa 0x0000000087f1b000
 .. .. ..0x0000000000003000: pte 0x0000000021fc68d7 pa 0x0000000087f1a000
 ..0x0000003fc0000000: pte 0x0000000021fc8401 pa 0x0000000087f21000
 .. ..0x0000003fffe00000: pte 0x0000000021fc8001 pa 0x0000000087f20000
 .. .. ..0x0000003fffffd000: pte 0x0000000021fd4813 pa 0x0000000087f52000
 .. .. ..0x0000003fffffe000: pte 0x0000000021fd00c7 pa 0x0000000087f40000
 .. .. ..0x0000003ffffff000: pte 0x000000002000184b pa 0x0000000080006000
```

Las líneas con una o dos marcas `..` son PTEs de los niveles 2 y 1 (apuntan a otra tabla). Las páginas finales son las de tres marcas. Los bits de las banderas son V=0x1, R=0x2, W=0x4, X=0x8, U=0x10, A=0x40 y D=0x80.

| Dirección virtual | Flags | Qué contiene | Permisos |
|---|---|---|---|
| `0x0` | `0x5b` | Texto (código) | V R X U (A lo puso el hardware al ejecutar) |
| `0x1000` | `0x17` | Datos | V R W U |
| `0x2000` | `0x07` | Página de guarda | V R W, **sin U** |
| `0x3000` | `0xd7` | Pila de usuario | V R W U (A y D encendidos por el uso) |
| `0x3fffffd000` | `0x13` | USYSCALL | V R U: legible, no escribible |
| `0x3fffffe000` | `0xc7` | Trapframe | V R W, sin U: solo lo usa el kernel y la trampolina |
| `0x3ffffff000` | `0x4b` | Trampolín | V R X, sin U: código que ejecuta la trampolina al entrar y salir del kernel |

El heap está vacío en este proceso, por eso no aparecen páginas entre la pila y USYSCALL.

## 2. Página de guarda

`exec()` deja la página de guarda justo debajo de la pila con `PTE_U` apagado (`uvmclear()`), aunque sigue siendo válida. La pila crece hacia abajo. Si un programa la desborda, su siguiente acceso cae en la guarda. En modo usuario el hardware prohíbe acceder a una página sin `PTE_U`, así que se produce un fallo de página y `usertrap()` mata al proceso. Sin la guarda, la pila pisaría en silencio los datos del programa y los errores serían mucho más difíciles de diagnosticar.

## 3. Otras llamadas que se pueden acelerar

Se pueden acelerar las llamadas que **solo consultan** un dato que el kernel puede dejar listo en una página de lectura y que no necesitan privilegios ni cambian estado.
- `getpid()`: el pid no cambia durante la vida del proceso (ya implementado).
- `uptime()`: solo devuelve `ticks`. Habría que mantener una copia en una página compartida actualizada por el kernel en cada tick (idealmente una sola página global y no una por proceso).
- `sbrk(0)`: solo consulta el tamaño. El kernel tendría que actualizar el dato cada vez que cambia `p->sz`, lo que añade costo en las llamadas que lo modifican.

No se pueden acelerar las que modifican estado del kernel o requieren su control: `fork`, `exec`, `exit`, `wait`, `kill`, `sbrk(n≠0)`, `pause`, `open`, `close`, `read`, `write`, `pipe`, `dup`, `chdir`, `mkdir`, `link`, `unlink`, `mknod`, `fstat`, `sync`. Cambian tablas del kernel (procesos, archivos, memoria), bloquean al proceso o tocan dispositivos. Todo eso exige ejecutar en modo supervisor.

## 4. Riesgo de USYSCALL con PTE_W

Cualquier proceso podría escribir en la página. Podría falsificar su propio pid, y `ugetpid()` mentiría. El problema crece si esa página llega a tener más campos o si el kernel o las bibliotecas confían en lo que contiene: el usuario podría corromper esa información y suplantar identidades o alterar decisiones. Además, la página es memoria física del kernel (`p->usyscall`), así que una escritura de usuario no debería poder alterarla. Por eso se mapea solo `PTE_R | PTE_U`, y un intento de escritura termina con el proceso muerto.

## 5. pgaccess() y páginas perezosas

`sbrklazy()` solo aumenta `p->sz`. No reserva memoria ni crea PTE válida hasta el primer acceso (la reserva `vmfault()` en el fallo de página). Sin PTE válida el hardware nunca pudo encender `PTE_A`, y como nadie tocó la página, es correcto reportarla como 0. Además, `pgaccess()` es una herramienta de observación y no debe cambiar lo que observa. Asignar memoria al consultarla gastaría RAM justo en las páginas que el proceso nunca usó, anularía la ventaja de la asignación perezosa, haría que consultar un rango grande agote la memoria y alteraría el resultado de `vmprint()`. Por eso el código recorre con `walk(..., 0)` (sin crear nada) y no pasa por `vmfault()`.

---

# Resultados de las pruebas

Salida completa en `pruebas/`.
- `pgtbltest`: **TODAS LAS PRUEBAS PASARON (11 de 11)**, y la revisión automática del formato de `vmprint()` da OK.
- `usertests -q`: **ALL TESTS PASSED**, sin `lost some free pages`.
- Compilación con `make qemu` sin advertencias del compilador (solo el aviso del enlazador *LOAD segment with RWX permissions*, que sale también en xv6 sin modificar con esta versión de binutils).
- El mensaje `usertrap(): unexpected scause 0xf` durante `usyscall_solo_lectura` es esperado.

---

# Decisiones de diseño

- **Una página por proceso, con puntero en `struct proc`.** Se reserva en `allocproc()` y se libera en `freeproc()`. `proc_freepagetable()` solo la desmapea, porque `exec()` crea una tabla nueva pero conserva `p->usyscall`.
- **Sin `PTE_W`.** Escribir en `USYSCALL` genera un fallo de página. Como la dirección está por encima de `p->sz`, `vmfault()` no intenta mapear nada y `usertrap()` mata al proceso. `copyout()` también rechaza esa página porque no tiene `PTE_W`.
- **`vmprint()` recursiva con la dirección virtual acumulada.** Cada PTE de nivel L cubre `2^(12+9L)` bytes. Una PTE válida sin R/W/X es un puntero a una tabla inferior, igual que en `freewalk()`.
- **`pgaccess()` en dos pasadas.** Primero se lee `PTE_A` y se arma la máscara. Después de que `copyout()` tuvo éxito, se limpia `PTE_A`. Así una falla de `copyout()` no hace perder accesos.
- **`sfence_vma()` tras limpiar `PTE_A`.** Si la entrada sigue en la TLB con A encendido, el hardware no volvería a marcarla en el siguiente acceso y se perderían accesos nuevos.
- **Validación sin desbordes.** El rango se verifica como `npages * PGSIZE > sz - base`, después de comprobar `base < sz`.

---

# Limitaciones conocidas

- La página compartida solo tiene el campo `pid`. No se aceleran otras llamadas (fuera del alcance).
- `pgaccess()` solo reporta hasta 64 páginas por llamada, como pide el enunciado.
- `PTE_A` solo lo mantiene el hardware. Los accesos que hace el propio kernel por dirección física (por ejemplo `copyout()` del resultado) no la encienden.
- No se usa `PTE_D` ni se implementa reemplazo de páginas.

---

# Declaración de uso de IA

Se usó Claude (Anthropic) como herramienta de apoyo para implementar el código de las Partes A, B y C, probarlo con las suites del curso y redactar este README. Los integrantes asumIMOS plena responsabilidad académica sobre el contenido entregado.
