# Problemas conocidos

- **Sin firma de Apple.** El instalador no está firmado ni notarizado: en otro Mac, macOS pedirá autorizarlo a mano.
- **Windows sin instalador.** `makensis` no arranca en este Mac sin Rosetta: se entrega un ZIP ejecutable, sin firmar, con el motor compilado con Zig.
- **Windows sin ejecutar.** El motor compila y enlaza para Windows x64 con Zig desde macOS, pero nunca se ha ejecutado en un PC, y la compilación con Visual Studio de la CI tampoco se ha lanzado. Allí no hay FFGL, ProRes, Bonjour ni porcentaje de GPU.
- **Grabación sin audio.** Vídeo solamente, también en el Clip Renderer.
- **Sin probar con hardware real:** cámaras, varias pantallas físicas, tiras LED y nodos Art-Net, controladores MIDI.
- **Pantallas solapadas en macOS, bases DisplayLink y GPU NVIDIA:** sin prueba; no hay hardware disponible.
- **HAP y FFmpeg:** no incluidos; los formatos dependen de AVFoundation.
- **Máscaras con curva automática:** la opción Curva pasa una curva suave por los puntos, sin asas Bezier manuales. No hay selección de varios puntos a la vez. Slice Transform y Slice Outline existen como ajustes de slice, no como efecto y fuente de composición.
- **Pantallas, faltan:** escala y rotación conjuntas de varios slices, Identify Slices y Show in Output.
- **No incluidos:** SMPTE, Ableton Link, NDI, Syphon/Spout, capturadoras y sincronización con reproductores DJ.
- **Capas y columnas:** reordenar, insertar, duplicar o quitar reinicia los clips que suenan, que vuelven a dispararse desde el principio. Los atajos asignados (MIDI, teclado, OSC) no siguen a la capa movida: mantienen su posición. Los grupos son fijos (4) y no tienen fila propia en la matriz.
- **Monitores:** sin elegir fuente. El crossfader no tiene modo de fusión propio. No se insertan decks entre otros ni se sacan paneles a otra ventana.
