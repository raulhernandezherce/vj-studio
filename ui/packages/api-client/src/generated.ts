// Generado por tools/gen_schema.py desde schemas/. No editar.
export type ParamType = 'bool' | 'int' | 'float' | 'string' | 'trigger' | 'enum';
export interface ParamDesc {
  path: string; type: ParamType; default?: boolean | number | string;
  min?: number; max?: number; readOnly?: boolean; label?: string; group?: string; unit?: string;
  options?: string[]; milestone: string;
}
export const SCHEMA_VERSION = 9;
export const PARAMETERS: readonly ParamDesc[] = [
  {
    "path": "/engine/version",
    "type": "string",
    "default": "0.10.1",
    "readOnly": true,
    "label": "Versión",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/engine/backend",
    "type": "string",
    "default": "none",
    "readOnly": true,
    "label": "Backend gráfico",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/engine/uptime",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1000000000000.0,
    "readOnly": true,
    "unit": "s",
    "label": "Tiempo activo",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/engine/fps",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1000,
    "readOnly": true,
    "unit": "fps",
    "label": "Fotogramas por segundo",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/engine/frames",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 9007199254740991,
    "readOnly": true,
    "label": "Fotogramas renderizados",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/engine/quit",
    "type": "trigger",
    "label": "Cerrar motor",
    "group": "Motor",
    "milestone": "M0"
  },
  {
    "path": "/output/opacity",
    "type": "float",
    "default": 1,
    "min": 0,
    "max": 1,
    "label": "Opacidad de salida",
    "group": "Salida",
    "milestone": "M0"
  },
  {
    "path": "/output/visible",
    "type": "bool",
    "default": true,
    "label": "Ventana visible",
    "group": "Salida",
    "milestone": "M0"
  },
  {
    "path": "/output/fullscreen",
    "type": "bool",
    "default": false,
    "label": "Pantalla completa",
    "group": "Salida",
    "milestone": "M0"
  },
  {
    "path": "/output/width",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 32768,
    "readOnly": true,
    "unit": "px",
    "label": "Ancho",
    "group": "Salida",
    "milestone": "M0"
  },
  {
    "path": "/output/height",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 32768,
    "readOnly": true,
    "unit": "px",
    "label": "Alto",
    "group": "Salida",
    "milestone": "M0"
  },
  {
    "path": "/composition/master",
    "type": "float",
    "default": 1,
    "min": 0,
    "max": 1,
    "label": "Maestro",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/bypass",
    "type": "bool",
    "default": false,
    "label": "Bypass",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/speed",
    "type": "float",
    "default": 1,
    "min": 0,
    "max": 10,
    "label": "Velocidad",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/width",
    "type": "int",
    "default": 1920,
    "min": 16,
    "max": 16384,
    "label": "Ancho",
    "group": "Composición",
    "milestone": "M1",
    "unit": "px"
  },
  {
    "path": "/composition/height",
    "type": "int",
    "default": 1080,
    "min": 16,
    "max": 16384,
    "label": "Alto",
    "group": "Composición",
    "milestone": "M1",
    "unit": "px"
  },
  {
    "path": "/composition/eject",
    "type": "trigger",
    "label": "Expulsar todo",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/columns/next",
    "type": "trigger",
    "label": "Columna siguiente",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/columns/previous",
    "type": "trigger",
    "label": "Columna anterior",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/columns/current",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Columna activa",
    "group": "Composición",
    "milestone": "M1",
    "readOnly": true
  },
  {
    "path": "/composition/beatsnap",
    "type": "enum",
    "default": "None",
    "options": [
      "None",
      "Beat",
      "Bar",
      "2 Bars",
      "4 Bars"
    ],
    "label": "Beat Snap",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/cliptarget",
    "type": "enum",
    "default": "Own Layer",
    "options": [
      "Own Layer",
      "Active Layer",
      "Free Layer"
    ],
    "label": "Clip Target",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/triggerstyle",
    "type": "enum",
    "default": "Normal",
    "options": [
      "Normal",
      "Toggle",
      "Piano"
    ],
    "label": "Trigger Style",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/activelayer",
    "type": "int",
    "default": 1,
    "min": 1,
    "max": 4096,
    "label": "Capa activa",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/structure/layers",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Capas",
    "group": "Composición",
    "milestone": "M1",
    "readOnly": true
  },
  {
    "path": "/composition/structure/columns",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Columnas",
    "group": "Composición",
    "milestone": "M1",
    "readOnly": true
  },
  {
    "path": "/composition/structure/groups",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Grupos",
    "group": "Composición",
    "milestone": "M1",
    "readOnly": true
  },
  {
    "path": "/composition/crossfader/phase",
    "type": "float",
    "default": 0.5,
    "min": 0,
    "max": 1,
    "label": "Crossfader",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/crossfader/curve",
    "type": "enum",
    "default": "Linear",
    "options": [
      "Linear",
      "Smooth",
      "Cut"
    ],
    "label": "Curva del crossfader",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/crossfader/behaviour",
    "type": "enum",
    "default": "Jump",
    "options": [
      "Jump",
      "Jump & Return"
    ],
    "label": "Botones A y B",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/tempo/bpm",
    "type": "float",
    "default": 120,
    "min": 20,
    "max": 500,
    "label": "BPM",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/composition/tempo/beat",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1000000000000.0,
    "label": "Beat",
    "group": "Composición",
    "milestone": "M1",
    "readOnly": true
  },
  {
    "path": "/composition/tempo/resync",
    "type": "trigger",
    "label": "Tiempo 1",
    "group": "Composición",
    "milestone": "M1"
  },
  {
    "path": "/project/path",
    "type": "string",
    "default": "",
    "label": "Archivo",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/name",
    "type": "string",
    "default": "Sin título",
    "label": "Nombre",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/dirty",
    "type": "bool",
    "default": false,
    "label": "Cambios sin guardar",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/canundo",
    "type": "bool",
    "default": false,
    "label": "Se puede deshacer",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/canredo",
    "type": "bool",
    "default": false,
    "label": "Se puede rehacer",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/decks",
    "type": "string",
    "default": "[]",
    "label": "Decks",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/project/folder",
    "type": "string",
    "default": "",
    "label": "Carpeta de documentos",
    "group": "Proyecto",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/composition/deck",
    "type": "int",
    "default": 1,
    "min": 1,
    "max": 4096,
    "label": "Deck actual",
    "group": "Composición",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/composition/notes",
    "type": "string",
    "default": "",
    "label": "Notas",
    "group": "Composición",
    "milestone": "M2"
  },
  {
    "path": "/preview/layer",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Capa en preview",
    "group": "Preview",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/preview/column",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 4096,
    "label": "Clip en preview",
    "group": "Preview",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/preview/file",
    "type": "string",
    "default": "",
    "label": "Archivo en preview",
    "group": "Preview",
    "milestone": "M2",
    "readOnly": true
  },
  {
    "path": "/preferences/confirmquit",
    "type": "bool",
    "default": true,
    "label": "Confirmar al salir",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/preferences/defaultblend",
    "type": "enum",
    "default": "Layer Determined",
    "options": [
      "Layer Determined",
      "Add",
      "Alpha",
      "Cut",
      "Darken",
      "Multiply",
      "Burn",
      "Lighten",
      "Screen",
      "Dodge",
      "Overlay",
      "Soft Light",
      "Hard Light",
      "Subtract",
      "Difference",
      "Difference I",
      "Exclusion",
      "Luma Key",
      "Luma Key I",
      "Luma is Alpha",
      "Displace",
      "50Mask",
      "RGB"
    ],
    "label": "Blend por defecto",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/preferences/defaulttransport",
    "type": "enum",
    "default": "Timeline",
    "options": [
      "Timeline",
      "BPM Sync"
    ],
    "label": "Transporte por defecto",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/preferences/defaultplaymode",
    "type": "enum",
    "default": "Loop",
    "options": [
      "Loop",
      "Ping Pong",
      "Random",
      "Play Once and Eject",
      "Play Once and Hold"
    ],
    "label": "Playmode por defecto",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/preferences/startupdelay",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 60,
    "label": "Retardo de arranque",
    "group": "Preferencias",
    "milestone": "M2",
    "unit": "s"
  },
  {
    "path": "/preferences/ffglfolders",
    "type": "string",
    "default": "",
    "label": "Carpetas FFGL (separadas por :)",
    "group": "Preferencias",
    "milestone": "M3"
  },
  {
    "path": "/preferences/uiscale",
    "type": "float",
    "default": 1,
    "min": 0.75,
    "max": 2,
    "label": "Tamaño de interfaz",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/preferences/thumbnails",
    "type": "bool",
    "default": true,
    "label": "Miniaturas",
    "group": "Preferencias",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k1",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 1",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k1/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 1",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k2",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 2",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k2/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 2",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k3",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 3",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k3/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 3",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k4",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 4",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k4/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 4",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k5",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 5",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k5/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 5",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k6",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 6",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k6/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 6",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k7",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 7",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k7/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 7",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k8",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 1,
    "label": "Perilla 8",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/dashboard/k8/links",
    "type": "string",
    "default": "[]",
    "label": "Enlaces perilla 8",
    "group": "Dashboard",
    "milestone": "M2"
  },
  {
    "path": "/composition/audio/volume",
    "type": "float",
    "default": 0,
    "min": -60,
    "max": 12,
    "label": "Volumen",
    "group": "Audio",
    "milestone": "M4",
    "unit": "dB"
  },
  {
    "path": "/composition/audio/pan",
    "type": "float",
    "default": 0,
    "min": -1,
    "max": 1,
    "label": "Panorama",
    "group": "Audio",
    "milestone": "M4"
  },
  {
    "path": "/audio/level/left",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 2,
    "label": "Nivel izquierdo",
    "group": "Audio",
    "milestone": "M4",
    "readOnly": true
  },
  {
    "path": "/audio/level/right",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 2,
    "label": "Nivel derecho",
    "group": "Audio",
    "milestone": "M4",
    "readOnly": true
  },
  {
    "path": "/audio/devices",
    "type": "string",
    "default": "[]",
    "label": "Dispositivos",
    "group": "Audio",
    "milestone": "M4",
    "readOnly": true
  },
  {
    "path": "/audio/status",
    "type": "string",
    "default": "parado",
    "label": "Estado",
    "group": "Audio",
    "milestone": "M4",
    "readOnly": true
  },
  {
    "path": "/audio/samplerate",
    "type": "int",
    "default": 48000,
    "min": 8000,
    "max": 192000,
    "label": "Frecuencia",
    "group": "Audio",
    "milestone": "M4",
    "readOnly": true,
    "unit": "Hz"
  },
  {
    "path": "/preferences/audio/output",
    "type": "string",
    "default": "",
    "label": "Salida de audio",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/input",
    "type": "string",
    "default": "",
    "label": "Entrada de audio",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/masterchannel",
    "type": "int",
    "default": 1,
    "min": 1,
    "max": 63,
    "label": "Canal master",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/previewchannel",
    "type": "int",
    "default": 3,
    "min": 1,
    "max": 63,
    "label": "Canal preview",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/inputchannel",
    "type": "int",
    "default": 1,
    "min": 1,
    "max": 63,
    "label": "Canal de entrada",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/inputgain",
    "type": "float",
    "default": 0,
    "min": -24,
    "max": 48,
    "label": "Ganancia de entrada",
    "group": "Preferencias",
    "milestone": "M4",
    "unit": "dB"
  },
  {
    "path": "/preferences/audio/buffersize",
    "type": "int",
    "default": 512,
    "min": 64,
    "max": 4096,
    "label": "Tamaño de búfer",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/fftgain",
    "type": "float",
    "default": 1,
    "min": 0,
    "max": 16,
    "label": "Ganancia maestra FFT",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/audio/enabled",
    "type": "bool",
    "default": true,
    "label": "Audio activo",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/preferences/bpm/nudgerange",
    "type": "float",
    "default": 0.1,
    "min": 0.01,
    "max": 0.5,
    "label": "Rango de nudge",
    "group": "Preferencias",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/tap",
    "type": "trigger",
    "label": "Tap",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/half",
    "type": "trigger",
    "label": "÷2",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/double",
    "type": "trigger",
    "label": "×2",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/pause",
    "type": "bool",
    "default": false,
    "label": "Pausa",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/nudgeup",
    "type": "bool",
    "default": false,
    "label": "Nudge +",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/nudgedown",
    "type": "bool",
    "default": false,
    "label": "Nudge −",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/metronome",
    "type": "bool",
    "default": false,
    "label": "Metrónomo",
    "group": "Composición",
    "milestone": "M4"
  },
  {
    "path": "/composition/tempo/metronomevolume",
    "type": "float",
    "default": -12,
    "min": -60,
    "max": 0,
    "label": "Volumen metrónomo",
    "group": "Composición",
    "milestone": "M4",
    "unit": "dB"
  },
  {
    "path": "/preferences/osc/input",
    "type": "bool",
    "default": false,
    "label": "Entrada OSC",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/inport",
    "type": "int",
    "default": 7000,
    "min": 1024,
    "max": 65535,
    "label": "Puerto de entrada",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/remote",
    "type": "bool",
    "default": false,
    "label": "Aceptar OSC de la red",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/output",
    "type": "bool",
    "default": false,
    "label": "Salida OSC",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/outhost",
    "type": "string",
    "default": "127.0.0.1",
    "label": "Destino",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/outport",
    "type": "int",
    "default": 7001,
    "min": 1024,
    "max": 65535,
    "label": "Puerto de salida",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/preset",
    "type": "enum",
    "default": "Output All",
    "options": [
      "Output All",
      "Solo asignados"
    ],
    "label": "Preset de salida",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/bundles",
    "type": "bool",
    "default": false,
    "label": "Usar bundles",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/zeroconf",
    "type": "bool",
    "default": true,
    "label": "Anunciar por ZeroConf",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/osc/status",
    "type": "string",
    "default": "apagado",
    "label": "Estado OSC",
    "group": "Preferencias",
    "milestone": "M5",
    "readOnly": true
  },
  {
    "path": "/preferences/midi/input",
    "type": "bool",
    "default": true,
    "label": "Entrada MIDI",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/midi/output",
    "type": "bool",
    "default": true,
    "label": "Salida MIDI",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/midi/clockin",
    "type": "bool",
    "default": false,
    "label": "Seguir reloj MIDI",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/midi/clockout",
    "type": "bool",
    "default": false,
    "label": "Enviar reloj MIDI",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/midi/middlec",
    "type": "int",
    "default": 3,
    "min": 0,
    "max": 5,
    "label": "Octava de Do central",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/midi/devices",
    "type": "string",
    "default": "[]",
    "label": "Dispositivos MIDI",
    "group": "Preferencias",
    "milestone": "M5",
    "readOnly": true
  },
  {
    "path": "/preferences/dmx/input",
    "type": "bool",
    "default": false,
    "label": "Entrada Art-Net",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/dmx/port",
    "type": "int",
    "default": 6454,
    "min": 1024,
    "max": 65535,
    "label": "Puerto Art-Net",
    "group": "Preferencias",
    "milestone": "M5"
  },
  {
    "path": "/preferences/dmx/status",
    "type": "string",
    "default": "apagado",
    "label": "Estado Art-Net",
    "group": "Preferencias",
    "milestone": "M5",
    "readOnly": true
  },
  {
    "path": "/shortcuts/preset",
    "type": "string",
    "default": "Por defecto",
    "label": "Preset de atajos",
    "group": "Atajos",
    "milestone": "M5",
    "readOnly": true
  },
  {
    "path": "/shortcuts/learning",
    "type": "bool",
    "default": false,
    "label": "Aprendiendo",
    "group": "Atajos",
    "milestone": "M5",
    "readOnly": true
  },
  {
    "path": "/composition/selectedclip",
    "type": "int",
    "default": 1,
    "min": 1,
    "max": 4096,
    "label": "Clip seleccionado",
    "group": "Composición",
    "milestone": "M5"
  },
  {
    "path": "/output/disabled",
    "type": "bool",
    "default": false,
    "label": "Salida desactivada",
    "group": "Salida",
    "milestone": "M6"
  },
  {
    "path": "/output/testcard",
    "type": "bool",
    "default": false,
    "label": "Carta de prueba",
    "group": "Salida",
    "milestone": "M6"
  },
  {
    "path": "/output/identify",
    "type": "bool",
    "default": false,
    "label": "Identificar pantallas",
    "group": "Salida",
    "milestone": "M6"
  },
  {
    "path": "/output/setup",
    "type": "string",
    "default": "",
    "label": "Escenario activo",
    "group": "Salida",
    "readOnly": true,
    "milestone": "M6"
  },
  {
    "path": "/output/displays",
    "type": "string",
    "default": "[]",
    "label": "Pantallas conectadas",
    "group": "Salida",
    "readOnly": true,
    "milestone": "M6"
  },
  {
    "path": "/record/active",
    "type": "bool",
    "default": false,
    "label": "Grabar",
    "group": "Grabación",
    "milestone": "M7"
  },
  {
    "path": "/record/codec",
    "type": "enum",
    "default": "H.264",
    "options": [
      "H.264",
      "HEVC",
      "ProRes 422"
    ],
    "label": "Códec",
    "group": "Grabación",
    "milestone": "M7"
  },
  {
    "path": "/record/fps",
    "type": "int",
    "default": 30,
    "min": 1,
    "max": 120,
    "label": "Fotogramas por segundo",
    "group": "Grabación",
    "milestone": "M7"
  },
  {
    "path": "/record/status",
    "type": "string",
    "default": "parado",
    "label": "Estado",
    "group": "Grabación",
    "readOnly": true,
    "milestone": "M7"
  },
  {
    "path": "/record/file",
    "type": "string",
    "default": "",
    "label": "Último archivo",
    "group": "Grabación",
    "readOnly": true,
    "milestone": "M7"
  },
  {
    "path": "/record/frames",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 1000000000000.0,
    "label": "Fotogramas escritos",
    "group": "Grabación",
    "readOnly": true,
    "milestone": "M7"
  },
  {
    "path": "/record/dropped",
    "type": "int",
    "default": 0,
    "min": 0,
    "max": 1000000000000.0,
    "label": "Fotogramas perdidos",
    "group": "Grabación",
    "readOnly": true,
    "milestone": "M7"
  },
  {
    "path": "/preferences/recordfolder",
    "type": "string",
    "default": "",
    "label": "Carpeta de grabación (vacía: Recorded)",
    "group": "Preferencias",
    "milestone": "M7"
  },
  {
    "path": "/dmx/enabled",
    "type": "bool",
    "default": false,
    "label": "Art-Net activo",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/target",
    "type": "string",
    "default": "255.255.255.255",
    "label": "Destino",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/port",
    "type": "int",
    "default": 6454,
    "min": 1,
    "max": 65535,
    "label": "Puerto",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/fps",
    "type": "int",
    "default": 30,
    "min": 1,
    "max": 44,
    "label": "Frecuencia",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/delay",
    "type": "int",
    "default": 40,
    "min": 0,
    "max": 1000,
    "unit": "ms",
    "label": "Retardo",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/artsync",
    "type": "bool",
    "default": true,
    "label": "ArtSync",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/dmx/status",
    "type": "string",
    "default": "apagado",
    "readOnly": true,
    "label": "Estado",
    "group": "DMX",
    "milestone": "M9"
  },
  {
    "path": "/engine/cpu",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 100000,
    "readOnly": true,
    "unit": "%",
    "label": "CPU del motor",
    "group": "Motor",
    "milestone": "M10"
  },
  {
    "path": "/engine/gpu",
    "type": "float",
    "default": -1,
    "min": -1,
    "max": 100,
    "readOnly": true,
    "unit": "%",
    "label": "GPU",
    "group": "Motor",
    "milestone": "M10"
  },
  {
    "path": "/engine/memory",
    "type": "float",
    "default": 0,
    "min": 0,
    "max": 10000000,
    "readOnly": true,
    "unit": "MB",
    "label": "Memoria del motor",
    "group": "Motor",
    "milestone": "M10"
  },
  {
    "path": "/project/recovered",
    "type": "bool",
    "default": false,
    "readOnly": true,
    "label": "Proyecto recuperado tras un fallo",
    "group": "Proyecto",
    "milestone": "M10"
  }
];
export const TEMPLATE_KEYS = ["layer", "group", "clip", "column", "groupcolumn"] as const;
