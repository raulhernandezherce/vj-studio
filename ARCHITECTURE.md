# Arquitectura

## Un programa, dos procesos

El usuario abre `VJ Studio.app`. Electron arranca `vjengine`, incluido en el paquete, y lo supervisa. Si la interfaz se cae, la salida sigue emitiendo; al reabrirla se reengancha al motor por el puerto 7777. Si el motor se cae, se reinicia hasta 5 veces en 30 s.

```
Interfaz (Electron + React)  ──WebSocket ws://127.0.0.1:7777/api/v1──▶  vjengine (C++20)
```

## Estructura

```
schemas/parameters.json    contrato de parámetros, con plantillas por capa, clip, grupo y columna
schemas/blendmodes.json    blend modes y transiciones como datos
schemas/effects.json       catálogo de efectos y fuentes como datos
engine/core                parámetros, eventos, módulos
engine/gfx                 IRenderBackend + OpenGL 4.1 + backend nulo
engine/media               IMediaSource, AVFoundation, ImageIO, generadores vj://
engine/audio               mezclador en tiempo real, FFT, efectos, CoreAudio
engine/animation           animación de parámetros y expresiones
engine/compositor          reloj de clip, composición, plan de render, renderer
engine/output              ventanas de salida, pantallas, slices y mallas
engine/shortcuts           atajos, MIDI, reloj MIDI y Art-Net
engine/control             servidor WebSocket/HTTP y ApiDispatcher
engine/ffgl                host de plugins FFGL
engine/project             documento, decks, historial, portapapeles, medios, preferencias
engine/app                 ejecutable vjengine: registra los módulos
ui/app                     Electron y ventana principal
ui/packages/api-client     cliente del motor y hooks de React
ui/packages/panel-registry registro de paneles
ui/packages/advanced-output ventana de pantallas y escenarios
ui/packages/panels-core    matriz, barra de composición e inspector
tests/                     unit, golden, protocol, smoke
tools/regress.sh           regresión acumulativa
packaging/                 instalador
```

Las demás carpetas del plan (`compositor`, `effects`, `audio`...) se crean en su hito.

## Composición (M1)

```
disparos, tempo, relojes → Composition::tick(dt) → RenderPlan → Renderer → destino → salida
```

- `Composition` no toca la GPU y el tiempo entra por `dt`, por lo que beat snap, autopilot y transiciones se prueban de forma determinista.
- Los oyentes solo encolan órdenes; `tick()` las aplica en el hilo principal.
- `Renderer` compone de abajo arriba (la capa 1 es la inferior) en color premultiplicado. Un grupo se pinta en su propio destino, sobre negro transparente, y se mezcla como una capa.
- `IMediaSource` es de acceso aleatorio: la fuente decodifica alrededor del tiempo pedido y guarda una caché de hasta 256 MB, lo que cubre reversa, ping-pong y saltos.
- La interfaz no conoce los parámetros uno a uno: `ParamForm` genera los controles desde lo que declara el motor.

## Proyecto (M2)

- `CommandBus` lleva lo que no es un parámetro (guardar, decks, deshacer) al hilo principal, entre fotogramas.
- El documento `.vjcomp` es JSON con solo los valores distintos del defecto, y una versión de formato con migraciones. Escribe en un temporal y renombra.
- Un deck guarda los valores de sus celdas. Cambiar de deck congela los ajustes de los clips que suenan, que siguen sonando, y los reengancha al volver.
- El historial guarda el valor anterior de un parámetro, o el documento entero en operaciones estructurales.
- Las perillas guardan sus enlaces en un parámetro, así que viajan con clips, decks y proyectos.
- Los monitores viajan como tramas binarias por el mismo WebSocket; solo se generan si alguien las pide, y un cliente lento salta fotogramas.
- Las rutas de archivo se guardan absolutas; el gestor de medios las reenlaza.

## Salida (M6)

- Un contexto OpenGL principal, oculto, hace todo el render. Cada pantalla física tiene una ventana con contexto compartido que solo presenta. Solo la primera espera al refresco: N esperas dividirían los fps entre N.
- Un escenario es un JSON con coordenadas normalizadas, independiente de la composición y de la resolución.
- Cada slice es una malla: entrada, rejilla de deformación y homografía de las cuatro esquinas se calculan por vértice en `buildMesh`, sin GPU.
- Una capa o grupo enrutado sale de la mezcla y se copia a un destino propio, como pide el manual.
- Cmd+Mayús+D apaga la salida como atajo global, y también desde la propia ventana de salida si la interfaz no responde.

## Grabación y cámaras (M7)

`media::VideoRecorder` escribe con AVAssetWriter en un hilo propio y una cola de 24 fotogramas. Si el disco no da abasto se pierde el fotograma y se cuenta en `/record/dropped`: la salida nunca espera al disco. También se descarta un fotograma de otro tamaño, por si la composición cambia a mitad de grabación. Abrir y cerrar el archivo ocurre en segundo plano, con 30 s como máximo para cerrar: el estado pasa por `preparando`, `grabando` y `cerrando`.

`OutputModule` lee la composición cada 1/fps. El Clip Renderer usa el preview: `previewHold` fija su reloj, cada tick espera a que el fotograma pedido esté decodificado, lo renderiza con sus efectos y avanza uno. Por eso tarda lo que tenga que tardar y no se salta fotogramas ni detiene la salida.

Las cámaras son una `IMediaSource` más (`vj://camera/<id>`, CameraSource.mm): guardan el último fotograma de AVCaptureSession. El permiso se pide al abrir la cámara, nunca al listarlas.

## Nodos (M8)

`engine/patch`: `Patch` es el modelo sin GPU; `NodeLibrary`, el registro, donde cada efecto del catálogo es un nodo; `Runtime` ejecuta una pasada por nodo de textura. `evaluationOrder` corta cada ciclo por una arista, que lee el fotograma anterior: por eso un bucle no puede bloquear. Un parche guardado entra en el catálogo con `glsl = "//PATCH"` y el Renderer lo delega a `PatchModule` por `IExternalEffects`, igual que FFGL. El editor (`ui/packages/node-editor`) no conoce la GPU: envía el parche entero con `patch.set` y ve el resultado por el monitor 2.

## DMX (M9)

`engine/dmx`: funciones puras (`autoAssign`, `render`, paquetes Art-Net) y `DmxModule`, que muestrea la composición a 256×144 y envía por UDP sin bloquear.

## Recuperación (M10)

Con cambios sin guardar, `ProjectModule` escribe cada 5 s `.recovery/recovery.vjcomp`. La salida limpia lo borra, así que si existe al arrancar hubo un fallo: se restaura en el primer tick, con todos los módulos en marcha.

## Windows

Lo específico de cada sistema queda detrás de las mismas interfaces: `IMediaSource`, `VideoRecorder`, `AudioDevice` y `CoreMidiPort`. En Windows las implementa `engine/platform-win` (Media Foundation, WIC, GDI, WASAPI y WinMM). Los sockets pasan por `core/Net.h`. OpenGL se carga con el glad que trae GLFW. FFGL no se compila en Windows.

## Control (M5)

- OSC, REST, WebSocket y atajos escriben en el mismo registro. `OscRouter` traduce las direcciones del manual a rutas internas sin tocar el contrato.
- `ShortcutEngine` no conoce dispositivos: recibe eventos con una clave y aplica el modo, y por eso se prueba sin hardware.

## Audio y animación (M4)

- `AudioEngine::render` corre en el hilo de audio: no reserva memoria ni toma bloqueos. Recibe un `AudioPlan` por intercambio atómico y devuelve el anterior por una cola, para liberarlo en el hilo principal.
- El reloj del clip manda: cada voz lee a su ritmo y salta si se aleja más de 50 ms de la posición que indica el plan.
- Los dispositivos se abren en tareas aparte: cambiar de salida no detiene el vídeo.
- `Animator` evalúa cada animación por fotograma y escribe con `setQuiet`, que no ensucia el proyecto ni entra en el historial.
- La entrada de audio solo se abre si el usuario eligió una, para no pedir el permiso del micrófono sin motivo.

## Efectos (M3)

- Un tipo de efecto es un dato: parámetros y una función GLSL. Añadir uno al catálogo no toca C++.
- Cada instancia crea sus propios parámetros en `<entidad>/effects/eN/`, así que API, deshacer y dashboard la tratan como cualquier otro control. El registro admite quitar parámetros por prefijo.
- `EffectChains` guarda el orden por entidad; `Renderer::applyChain` lo ejecuta tras clip, capa, grupo y mezcla final, en ese orden.
- Con blend Alpha el efecto sustituye al original según la opacidad; con otro blend se mezcla con él.
- Una fuente generativa es un efecto en cabeza de cadena sobre un lienzo vacío (`vj://source/<id>`).
- LUT y texto usan una textura de apoyo en `uTex1`. Un shader que no compila se salta y se anota: nunca detiene la salida.
- Los ISF de `Extra Effects` se traducen a GLSL 4.10 con macros de la especificación.

## FFGL

- El ABI se declara en `engine/ffgl/FfglAbi.h`: compilar el programa no necesita el SDK. El SDK oficial solo se descarga con las pruebas, para compilar plugins reales.
- Cada plugin se explora con `vjengine --ffgl-probe` en un proceso hijo con tiempo límite, y entra al catálogo como un efecto más.
- Las bibliotecas se cargan y las instancias se crean en el hilo de render, al primer uso. Una instancia que deja de pedirse se libera al final del fotograma.
- `IRenderBackend::runExternal` entrega el contexto en estado por defecto y lo restablece después. Solo existe en OpenGL: otro backend tendría que compartir textura con un contexto GL.

## Cómo se añade un hito

1. **Motor:** nueva carpeta con `vj_module(...)`, una línea `add_subdirectory` en `CMakeLists.txt` y un `modules.add(...)` en `engine/app/main.cpp`.
2. **Parámetros:** se declaran en `schemas/parameters.json` y se ejecuta `tools/gen_schema.py`. `tools/regress.sh` falla si el código generado no coincide.
3. **Interfaz:** un paquete en `ui/packages/` que llama a `panels.register(...)`.
4. **Pruebas:** se añaden a `tests/`. Las anteriores siguen ejecutándose.

## Reglas

- Solo `engine/gfx` conoce OpenGL. Metal o Direct3D 12 implementan `IRenderBackend`.
- Toda fuente de imagen implementa `IMediaSource`. FFmpeg se añade en `openMedia()`.
- Interfaz, API y, desde M5, OSC y MIDI usan las mismas acciones de `ApiDispatcher`.
- Solo el hilo principal toca GLFW y la GPU. Los oyentes de parámetros corren en el hilo que hizo el cambio y se comunican con el render mediante atómicos o colas.

## API

Cada petición es JSON: `{"id":1,"action":"set","parameter":"/output/opacity","value":0.5}`.
Acciones: `list`, `get`, `set`, `reset`, `trigger`, `subscribe`, `unsubscribe`, `command` y `monitor`. `subscribe` admite `"*"`.
El motor envía además `{"type":"update"}`, `{"type":"trigger"}` y `{"type":"event"}`.
`GET /api/v1/health` identifica el motor. Escucha solo en 127.0.0.1 y rechaza orígenes web que no sean locales.

## Disposición de la ventana

`App.tsx` compone las zonas `main` (matriz y decks), `top` (tempo), `lower` (monitores e inspectores fijos), `side` (explorador con pestañas) y `bottom` (opcional). El layout guardado es la versión 2; las disposiciones de versión 1 se ignoran y se usa la de defecto.
