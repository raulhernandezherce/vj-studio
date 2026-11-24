<div align="center">

# 🎛️ VJ Studio

**Software de VJ y mapeo de vídeo para directo: mezcla clips en tiempo real, aplica efectos, los sincroniza con la música y reparte la imagen entre varias pantallas, proyectores y luces DMX.**

Motor gráfico en C++20 y OpenGL 4.1 · interfaz en Electron y React · 27 suites de pruebas

[![Descargar](https://img.shields.io/github/v/release/raulhernandezherce/vj-studio?include_prereleases&label=descargar&color=F5A623)](https://github.com/raulhernandezherce/vj-studio/releases/latest)
![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![OpenGL](https://img.shields.io/badge/OpenGL-4.1-5586A4?logo=opengl&logoColor=white)
![Electron](https://img.shields.io/badge/Electron-React-47848F?logo=electron&logoColor=white)
![macOS](https://img.shields.io/badge/macOS-Apple_Silicon-000000?logo=apple&logoColor=white)

<img src="docs/manual/principal.png" width="900" alt="Ventana principal de VJ Studio: matriz de clips por capas y columnas, crossfader, barra de tempo, monitores y paneles de propiedades">

</div>

---

## Qué hace

| | |
|---|---|
| 🎬 **Matriz de clips** | Capas y columnas (hasta 32 × 64) con disparo por clip o por columna, decks, grupos y crossfader A/B. Más de 100 fps a 1080p en un Mac con Apple Silicon. |
| ✨ **Efectos** | Catálogo de 57 efectos y fuentes, blend modes, LUT, shaders ISF y plugins **FFGL**. Los efectos se arrastran a las celdas. |
| 🎵 **Música** | Tempo con tap y resync, beat snap, análisis de audio por FFT y animación de cualquier parámetro sincronizada con el BPM. |
| 🖥️ **Pantallas** | Varias salidas con slices, máscaras, deformación por malla, edge blending para unir proyectores, retardo por pantalla y enrutado por bus del crossfader. |
| 🧩 **Editor de nodos** | Unos 90 nodos para crear efectos y fuentes propios, con instancias, FFT, secuenciador y conexión a cualquier parámetro. |
| 💡 **Luces** | Salida DMX por Art-Net, editor de fixtures y lumiverses que convierten la imagen en luz para tiras LED. |
| 🎚️ **Control** | MIDI (con reloj), OSC, REST, WebSocket y atajos de teclado asignables a cualquier parámetro. |
| ⏺️ **Grabación** | Grabación de la salida en H.264, HEVC o ProRes 422 y un Clip Renderer que renderiza un clip con sus efectos sin parar el directo. |

<p align="center">
  <img src="docs/manual/pantallas.png" width="49%" alt="Ventana de pantallas con slices y deformación">
  <img src="docs/manual/nodos.png" width="49%" alt="Editor de nodos">
</p>
<p align="center">
  <img src="docs/manual/efectos.png" width="32%" alt="Panel de efectos">
  <img src="docs/manual/animacion.png" width="32%" alt="Animación de parámetros">
  <img src="docs/manual/dmx.png" width="32%" alt="DMX y fixtures">
</p>

## Descargar

En [**Releases**](https://github.com/raulhernandezherce/vj-studio/releases/latest) están el instalador para macOS (Apple Silicon), una compilación experimental para Windows y el manual en PDF.

> [!NOTE]
> El instalador no está firmado por Apple. La primera vez, abre la app con clic derecho → **Abrir**. La versión de Windows no se ha probado todavía en un PC real. Detalles en [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

## Arquitectura

Un programa, dos procesos. Si la interfaz se cae, la salida sigue emitiendo, y al reabrirla se reengancha al motor. Si el motor se cae, Electron lo reinicia y se recupera la última copia automática.

```mermaid
flowchart LR
    UI["Interfaz<br/>Electron + React"] <-- "WebSocket 127.0.0.1:7777<br/>parámetros · órdenes · monitores" --> E["vjengine<br/>C++20"]
    E --> C["Compositor<br/>RenderPlan → Renderer"]
    C --> GL["OpenGL 4.1"]
    GL --> O["Pantallas y proyectores"]
    E --> D["DMX / Art-Net"]
    M["MIDI · OSC · REST · audio FFT"] --> E
```

- **Los parámetros son datos.** El motor declara cada parámetro en [`schemas/`](schemas) y la interfaz genera los controles a partir de esa declaración. Añadir un efecto no obliga a tocar la interfaz.
- **Composición determinista.** El tiempo entra por `dt` y la composición no toca la GPU, así que el beat snap, el autopilot y las transiciones se prueban sin pantalla.
- **Pruebas en capas.** Unitarias, *golden* (comparación de imágenes renderizadas), de protocolo extremo a extremo y *smoke* de la app empaquetada. `tools/regress.sh` repite las de todos los hitos.

Más detalle en [ARCHITECTURE.md](ARCHITECTURE.md). Estado de cada hito en [ROADMAP.md](ROADMAP.md).

## Compilar (macOS, Apple Silicon)

```bash
tools/regress.sh --app
```

Instala CMake y Node dentro de `.toolchain/`, compila, pasa todas las pruebas y genera `dist/mac-arm64/VJ Studio.app` y el instalador `dist/VJ Studio-<versión>-arm64.dmg`.

```bash
open "dist/mac-arm64/VJ Studio.app"
```

Solo el instalador, sin pruebas: `cmake --build build --target app`.

---

<sub>Hecho por <a href="https://github.com/raulhernandezherce">Raúl Hernández Herce</a> · «VJ Studio» es un nombre provisional</sub>
