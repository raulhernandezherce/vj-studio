# Roadmap

Todos los hitos se integran en el mismo programa. Un hito se cierra solo con `tools/regress.sh --app` en verde, que repite las pruebas de todos los anteriores.

| Hito | Contenido | Estado |
|---|---|---|
| M0 | Parámetros, API WebSocket, abstracción gráfica, ventana de salida, clip real, instalador | **Cerrado en macOS arm64** |
| M1 | Clips en capas, disparo, blend modes, transporte, grupos, composición | **Cerrado en macOS arm64, con parciales** |
| M2 | Proyectos, decks, deshacer, monitores, miniaturas, medios, dashboard, layouts | **Cerrado en macOS arm64, con parciales** |
| M3 | Cadenas de efectos, catálogo, presets, LUT, ISF, fuentes y texto | **Cerrado en macOS arm64, con parciales** |
| M4 | Audio, FFT, efectos de audio, BPM, animación de parámetros | **Cerrado en macOS arm64, con parciales** |
| M5 | REST, OSC, MIDI, DMX de entrada, atajos | **Cerrado en macOS arm64, con parciales** |
| M6 | Advanced Output: pantallas, escenarios, slices, deformación, edge blending, enrutado | **Cerrado en macOS arm64, con parciales** |
| M7 | Grabación, Clip Renderer y cámaras (0.8.0) | Parcial: NDI, Syphon y capturadoras en espera |
| M8 | Editor de nodos | Hecho salvo Syphon, slices y nodos MIDI/OSC propios |
| M9 | DMX, fixtures y Art-Net | Parcial: sincronía DJ, SMPTE y 10 bits en espera |
| M10 | Rendimiento, recuperación, instalador | Parcial: firma y Windows en espera |

## M0: comprobado

CI-01 (modelo de parámetros, revisado en M1 con el artículo completo) y CI-07 (API WebSocket). El programa completo pasa las pruebas de interfaz a motor, motor a interfaz, recuperación de una caída y reenganche.

## M1: estado por requisito

Datos de la última ejecución: 123 pruebas del motor, 17 de interfaz, extremo a extremo y programa real, en desarrollo y empaquetado.

| Requisito | Estado | Queda fuera |
|---|---|---|
| RP-03 Disparo, RP-04 Columna, RP-05 Beat Snap, RP-06 Clip Target, RP-09 Ignorar columna | Hecho | |
| RP-07 Trigger Style | Hecho con clic, tecla y API | Nota MIDI: M5 |
| RP-08 Fader Start, RP-16 Autopilot, RP-17 Transición por clip, RP-19 Propiedades | Hecho | Autopilot por columna; Random usa orden no reproducible |
| RP-10 Ajuste de tamaño | Hecho | La prueba usa 16:9 en 4:3 y sondas de píxel, no imagen de referencia |
| RP-20 Transporte | Hecho: Timeline, BPM Sync, dirección, velocidad, duración, entrada, salida y 5 playmodes | SMPTE y Denon: M9. Prueba con reloj y vídeo de 2 colores, no con fotogramas numerados |
| RP-26 Blend modes | 22 modos | Las fórmulas son estándar o propias: el manual no las da. Coinciden con nuestra referencia (±1,5/255), no con Resolume. Faltan los 50 Blends |
| RP-27 Transformación | Por clip, capa y grupo | Rotación X/Y; composición |
| RP-28 Máscaras | 50Mask, All Below y One Below | Transformación de la capa máscara en One Below |
| RP-31 Imágenes | PNG con alfa, JPG y otros de ImageIO | |
| RP-21 Capas | Parcial | Efectos: M3. Añadir, quitar y reordenar capas |
| RP-22 Grupos | Parcial: render aislado, blend, opacidad, bypass, solo, eject, lock, velocidad, columna | Efectos: M3 |
| RP-24 Composición | Parcial: resolución, maestro, velocidad, columnas, crossfader como ganancia por bus | Efectos globales: M3. Curva, blend y botones del crossfader. Tasa y 16 bits |
| RP-01 Clips | Parcial: vídeo, imagen y generadores | HAP y FFmpeg; audio: M4; cámara: hecha, sin probar con cámara real |

Transiciones disponibles: Alpha, Cut, Wipe Left, Wipe Up, Wipe Ellipse, Dissolve y Push Up. El manual no da la lista completa. Disparar con la capa vacía no hace fundido de entrada.

**Corrección de la matriz:** RP-21 dice que la capa 1 queda arriba; el manual dice que es la de abajo. El programa sigue el manual.

**Cambio de API:** `/media/*` desaparece; cada clip vive en `/composition/layers/L/clips/C/`. Las opciones `--media` y `--seek` del motor se retiran.

**Rendimiento:** unos 110 fps en la composición de 1080p con 4 capas, con ventana oculta y contenido ligero (generadores y vídeo de 320 px). Sin medir con 4 vídeos 1080p ni 4K.

## M2: estado por requisito

Datos de la última ejecución: 153 pruebas del motor, 23 de interfaz, extremo a extremo y programa real, en desarrollo y empaquetado.

| Requisito | Estado | Queda fuera |
|---|---|---|
| HP-01 Guardar y abrir | Hecho: imagen idéntica byte a byte, escritura atómica y migraciones | Efectos: M3. Qué clips sonaban no se guarda |
| HP-02 Deshacer | Hecho: 100 operaciones sin divergencia. Excluye disparos, API y automatismos, como el manual | Historial propio de Advanced Output: M6 |
| HP-04 Medios | Hecho: 20 archivos en una operación, reemplazar y recopilar | Aplica la corrección sin preguntar Fix All o selección |
| HP-05 Monitores | Hecho: Preview y Output a 480×270 y 25 fps | Elegir fuente del monitor, duplicar monitores |
| HP-08 Carpetas | Hecho, en Documentos/VJ Studio | |
| RP-02 Carga | Hecho: panel Archivos, favoritos, miniaturas, doble clic | 50 clips en 0,2 s, pero con vídeos de 320 px |
| RP-12 Persistentes, RP-15 Colores, RP-23 Decks, RP-34 Notas y tiempo | Hecho | Color de varios clips a la vez; traer un deck de otra composición |
| RP-33 Dashboard | Hecho: 8 perillas por clip, capa, grupo y composición | Entrada, salida e inversión solo por API; renombrar perilla; color |
| RP-11 Miniaturas | Parcial: actual, desde archivo, original, Snapshot y copiar imagen | Con efectos: M3. Pegar la imagen en un clip |
| RP-13 Copiar y pegar | Parcial: cortar, copiar, pegar, mover, intercambiar, renombrar | Paste Effects: M3. Selección múltiple |
| RP-14 Mostrar y quitar | Parcial: Mostrar en Finder | Quitar pistas: M4. Mostrar en el navegador propio |
| CI-10 Preferencias | Parcial: salida, retardo guardado, blend, transporte, playmode, tamaño de interfaz | Retardo sin aplicar al arranque. Fuente, desplazamiento, panel de clip y actualizaciones |
| HP-03 Layouts | Parcial: mover por zonas, ocultar, redimensionar, guardar y cargar | Arrastrar paneles, desacoplar en otra ventana |

**Corregido de M1:** cada vídeo cargado retenía hasta 256 MB de fotogramas aunque no sonara. Ahora los libera al dejar de reproducirse: 50 clips cargados ocupan 168 MB y 255 MB con 7 sonando (vídeo de 320 px).

**Rendimiento:** unos 110 fps con la columna en vivo y los monitores activos, con ventana oculta y vídeo ligero. Sigue sin medirse con vídeo 1080p o 4K.

## M3: estado por requisito

Datos de la última ejecución: 189 pruebas del motor, 23 de interfaz, extremo a extremo y programa real, en desarrollo y empaquetado.

| Requisito | Estado | Queda fuera |
|---|---|---|
| EF-02 Cadenas | Hecho: clip, capa, grupo y composición; orden, bypass, opacidad, blend, quitar, copiar y pegar. Con deshacer y guardado | Effect Clips, renombrar y color en la interfaz, arrastrar para reordenar |
| EF-06 Presets | Hecho: por efecto y de cadena; aplicar copia valores una vez | |
| EF-07 LUT | Hecho: .cube 3D con interpolación | Interpola en azul y deja bilineal rojo y verde: no es trilineal exacta en LUT muy pequeñas |
| EF-08 ISF | Parcial: efectos y fuentes de una pasada desde Extra Effects | Mezcladores, varias pasadas, imágenes extra, audio |
| RP-13 Paste Effects | Hecho | Selección múltiple |
| RP-21, RP-22, RP-24 | Se completan los efectos por capa, grupo y composición | Lo demás, según la tabla de M1 |
| EF-01 Catálogo | Parcial: 32 efectos y 8 fuentes | El requisito pide más de cien. El manual no publica la lista: nombres y fórmulas son propios |
| RP-29 Fuentes y texto | Parcial: 8 fuentes y texto editable por parámetro | Animadores de texto. Envío por OSC: M5 |
| EF-04 FFGL | Hecho para efectos y fuentes: Extra Effects y carpetas de Preferencias. Probado con 4 plugins del SDK oficial, incluido uno con transform feedback | Mezcladores FFGL, parámetros de texto, archivo y buffer, opciones como lista. Probado solo con los ejemplos del SDK, no con plugins comerciales |
| EF-03 Efectos de audio, EF-05 VST | Pasan a M4 | Necesitan el motor de audio |

FFGL en macOS arm64: un plugin x86_64 no carga. Rendimiento medido con ventana oculta a 1080p: Gradient, 111 fps; con Particles encima, 11 fps. Sin medir con plugins comerciales. Una caída al explorar se aísla en un proceso aparte; **una caída durante el render tumba el motor**, que el supervisor reinicia perdiendo lo no guardado.

Las referencias de EF-01 las genera el propio programa: detectan regresiones, no demuestran que un efecto sea correcto. Se revisaron a ojo en una hoja de contacto, lo que destapó dos errores que las pruebas no veían.

## M4: estado por requisito

| Requisito | Estado | Queda fuera |
|---|---|---|
| AS-01 Mezcla, RP-18 Audio del clip | Hecho: dB y panorama por clip, capa, grupo y composición, limitador de master | |
| AS-04 FFT | Hecho: fuente externa, composición, clip, capa o grupo; L, M, H o rango; ganancia, caída y 4 direcciones | Rango libre solo por API: la interfaz ofrece L, M y H |
| AS-05 BPM | Hecho salvo autodetección: Tap, Resync, ÷2, ×2, pausa, nudge y metrónomo | |
| AS-07, AS-08, AS-09, AS-10, AS-11 Animación | Hecho: Timeline, BPM Sync, posición de clip, FFT, crossfader, Dashboard, expresiones, envolventes editables y arranque por composición, clip, columna o a mano. Bool, lista, entero y evento | Color interpolado y modo Basic. Duración ligada a entrada y salida |
| EF-09 Presets de animación, RP-25 Crossfader como fuente, EF-03 Efectos de audio | Hecho: 7 efectos con Dry/Wet | Curva y modos del crossfader, como en M1 |
| AS-02 Preferencias de audio | Parcial: dispositivo, canales, búfer y cambio en caliente | Frecuencia de muestreo interna fija en 48 kHz. ASIO: Windows |
| AS-03 Entrada | Parcial: dispositivo, canal y ganancia, probados inyectando señal | SMPTE: M9. Sin probar con un micrófono real, para no pedir permisos en las pruebas |
| RP-14 Quitar pistas | Hecho: audio y vídeo, sin eliminar el clip | |
| RP-01 Clips | Se añaden clips solo de audio | HAP, FFmpeg y cámara |
| AS-06 Ableton Link | Sin hacer | Requiere SDK y decidir licencia: GPL o comercial |
| EF-05 VST | Sin hacer | Requiere SDK VST3 |

El audio se decodifica entero a memoria, hasta 30 minutos por clip (unos 345 MB); de ahí en adelante se corta. Bypass y Solo ocultan vídeo pero no silencian: el manual no aclara qué deben hacer. Las animaciones de clips no viajan con un clip congelado por cambio de deck.

## M5: estado por requisito

| Requisito | Estado | Queda fuera |
|---|---|---|
| CI-04 OSC de entrada | Hecho: ejemplos del manual, float normalizado, int, texto, a, +, -, *, ?, ZeroConf | Color: ningún parámetro lo es aún. `/composition/video/effects/transform/scale` no existe: la composición no tiene transformación propia |
| CI-05 OSC de salida | Parcial: Output All, solo asignados, destinos por IP y bundles | Comodines y dirección personalizada |
| CI-08 REST | Hecho: cada ruta del esquema se prueba. No es compatible con la de Resolume | Añadir o quitar capas y columnas |
| CI-02 Teclado, CI-03 MIDI | Hecho: aprendizaje, modos, monitor y retorno a LED. Probado con puertos MIDI virtuales | Middle C sin aplicar. Sin controlador físico |
| CI-06 DMX de entrada | Hecho: Art-Net, probado por UDP local | Con mesa real, GrandMA2, Clip Trigger por rangos, Channel Offset |
| AS-12 Reloj MIDI | Parcial: entrada, salida, deriva menor de 1 beat en 10 min simulados | Sin dispositivo real |
| CI-09 Mapeos | Parcial: presets exportables e importables en JSON, no XML | Perfiles como Akai APC64 |

OSC y Art-Net están apagados por defecto: no tienen autenticación. OSC escucha solo en local hasta activar "Aceptar OSC de la red".

## M6: estado por requisito

Un escenario es una configuración de pantallas guardada en Presets/Screens, independiente de la composición.

| Requisito | Estado | Queda fuera |
|---|---|---|
| SM-02 Pantallas, SM-03 Entrada, SM-04 Mapas, SM-06 Transformación, SM-09 Enrutado, SM-11 Escenarios | Hecho. Escenarios en JSON, no XML | Rotación de slice en la salida: se hace moviendo las esquinas |
| SM-05 Deformación | Hecho: perspectiva, rejilla lineal y Bezier | Comparar con una superficie real |
| SM-07 Polígonos y máscaras | Hecho: polígonos y máscaras poligonales de pantalla | Dibujar polígonos a mano en la interfaz: hay formas predefinidas. Editar puntos de máscara |
| SM-08 Edge blending | Hecho: variación ≤5 % medida en imagen, no con proyectores | Comprobar con 2 proyectores reales |
| SM-10 Pantallas y carta de prueba | Hecho por software | **Sin probar con una segunda pantalla real** |
| SM-01 Advanced Output, SM-12 Muchas salidas | Parcial: físicas y virtuales | Syphon, Spout, NDI y capturadoras: pendientes. Sin probar 8 salidas físicas |

Las ventanas físicas solo se han probado a través de la lógica: esta máquina tiene una pantalla y las pruebas no abren ventanas a pantalla completa.


## M7: Entradas, salidas y grabación

| Requisito | Estado | Falta |
|---|---|---|
| SM-21 Grabación | Hecho: botón Rec, H.264, HEVC y ProRes 422, carpeta `Recorded` | Sin audio en el archivo |
| RP-32 Clip Renderer | Hecho: clip con sus efectos a archivo, fuera de tiempo real y sin parar la salida | Sin audio; sin elegir códec desde el menú |
| RP-30 Cámaras en vivo | Hecho en código: listado y `vj://camera/<id>` | Sin probar con cámara real: las pruebas no abren la cámara para no pedir permiso |
| SM-19 Syphon | En espera | Framework de terceros; necesita Xcode para compilarlo |
| SM-20 NDI | En espera | SDK propietario |
| SM-18 Capturadoras | En espera | Hardware para probar; las que macOS ve como cámara ya salen en el listado |
| Spout | No aplica en macOS | Windows |

## Pendiente de plataforma y comprobación manual

- **Windows:** no compila todavía. Faltan el cargador OpenGL, los sockets Winsock y un decodificador. El CI solo cubre macOS y aún no se ha ejecutado en GitHub.
- **Formatos:** solo los que leen AVFoundation e ImageIO. FFmpeg y HAP entran como otro `IMediaSource`.
- **Comprobación manual pendiente:** ventana de salida, pantalla completa con dos monitores, icono del motor en el Dock, diálogos de archivo, arrastrar y soltar, atajos de teclado, Mostrar en Finder y copiar imagen. Tampoco se ha probado con vídeos largos propios.
- **Reinicio del motor:** tras una caída vuelve a una composición vacía y se pierde lo no guardado. No hay autoguardado ni recuperación: M10.
- **Firma:** el instalador va con firma ad hoc. Abierto en otro Mac, Gatekeeper lo bloqueará hasta M10.
- **API:** las acciones son las de la matriz, pero el formato de mensajes es propio y no compatible con el de Resolume.

## M8: Editor de nodos

| Requisito | Estado | Falta |
|---|---|---|
| WN-01 a WN-04 Nodos, conexiones, categorías y tipos | Hecho, con bucles retardados un fotograma y tipos incompatibles rechazados | |
| WN-05 Visibilidad | Hecho: H y J, con Mayús para todos | |
| WN-06 Atributos | Hecho: resolución y 8 o 16 bits | |
| WN-07 Instancias | Hecho: Rectangle en rejilla, 1000 instancias probadas | Solo esa forma |
| WN-08 Integración | Parcial: efecto y fuente, con Float, Integer y Trigger expuestos | Mezclador; Color como parámetro |
| WN-09 Texture Input y Output | Hecho | Enviar directamente a una pantalla |
| WN-10 Nodos generales | Parcial: unos 90 nodos, sin arrays | Arrays; imágenes de comparación |
| WN-11 FFT, MIDI y OSC | Parcial: secuenciador, Falloff, Frequency to Pitch y BPM; MIDI y OSC por el nodo Parameter | Espectro por bandas; nodos MIDI y OSC propios |
| WN-12 Syphon y Spout | En espera | Depende de M7 |
| WN-13 ISF | Parcial: shader con valores por defecto | Entradas del ISF como puertos |
| WN-14 Slices | Pendiente | Las pantallas no son parámetros todavía |
| WN-15 Guardar y consolidar | Hecho, con aviso de versión mayor | |
| WN-16 Parcheo rápido | Hecho: Tab, cable al vacío, duplicar | |

## M9: DMX y sincronía

| Requisito | Estado | Falta |
|---|---|---|
| SM-13 Lumiverses | Hecho: Auto Span y Align Output, con brillo y contraste | Probar con tira LED real |
| SM-14 Fixtures | Hecho: tipos, canal, universo, volteo y aviso de solapes | Interruptores DIP |
| SM-15 Art-Net y ArtSync | Parcial: difusión o IP, frecuencia y retardo de 40 ms, probado en local | Detección de nodos, elegir adaptador, prueba con nodo real |
| SM-16 Editor de fixtures | Hecho | |
| AS-13 SMPTE | Pendiente | Decodificador LTC |
| AS-14 StageLinQ, AS-15 Pro DJ Link | En espera | Hardware y protocolos no oficiales |
| SM-17 10 bits | En espera | Pantalla y GPU compatibles |

## M10: Producto

| Requisito | Estado | Falta |
|---|---|---|
| HP-06 Rendimiento | Hecho: FPS, CPU, GPU y memoria en la barra de estado | Comparar con el Monitor de Actividad al 5 % |
| HP-07 Recuperación | Hecho: copia cada 5 s con cambios; se restaura tras un cierre brusco | |
| HP-09 Instalación | Parcial: DMG para macOS | Firma y notarización (cuenta de Apple), Windows |
| HP-10 Problemas conocidos | Parcial: ver `KNOWN_ISSUES.md` | Probar con hardware variado |

## Espacio de trabajo tipo mesa de VJ

Interfaz reorganizada: matriz de clips a todo el ancho, decks y crossfader debajo, barra de tempo, y abajo monitores apilados, propiedades de composición y capa, propiedades de clip, explorador y ayuda contextual. Capas y columnas se añaden y quitan en vivo (hasta 32 × 64), las capas se renombran y se guardan en la composición.

Catálogo de 57 efectos y arrastre de efectos y fuentes a las celdas. La ventana de pantallas suma zoom, imán, rejilla, deshacer y rehacer, medidas en píxeles, igualar entrada y salida, duplicar, plegar y activar pantallas, edición de puntos de polígono, nombre de slice, guía de imagen y ayuda contextual.

La ventana de pantallas añade retardo por pantalla, fuentes por bus A/B del crossfader, posición, escala y rotación del contenido dentro del slice, contorno de forma, selección múltiple de slices y mover el lienzo.

Efectos con memoria por instancia (63 efectos en total): Estelas, Eco con zoom, Arrastre de píxeles, Congelado al ritmo, Retardo de imagen y Retardo RGB. La memoria se reserva al usarlos y se libera al quitarlos o desactivarlos.

Ventana de pantallas: asas de escala y rotación, lazo, Mayús + clic, imán entre slices (Ctrl lo desactiva), flechas de 1 y 10 píxeles, campos con cálculos, menú de clic derecho, dividir slices, mover slices entre pantallas, dibujar polígonos, máscaras editables por puntos, resoluciones predefinidas, rejillas hasta 16 × 16 y gamma RGB en edge blending. Crossfader con curva y comportamiento de botones.

Capas y columnas: insertar, duplicar, mover, vaciar y quitar cualquiera, con deshacer; las rutas de pantallas, los enlaces del dashboard y las animaciones siguen a la capa. Plegar capas, asignar grupos desde el menú y menús Composición, Deck, Capa, Columna y Clip.

Ventana de pantallas: menú de slice completo (centrar, reflejar, mitades, toda el área, triángulo, círculo, intercambiar entrada y salida, orden, cortar, copiar y pegar), máscaras por slice con curva, modificadores del manual (Mayús, Alt, Ctrl), ⌘D, Retroceso, rueda, ⌘+/−, Espacio y rejilla LED 8×8. Una prueba con ratón y teclado reales recorre arrastrar, rotar, lazo, polígonos, máscaras, salida y plegado.

Pendiente respecto al manual consultado: ver `KNOWN_ISSUES.md`.
