# AC Global Overlay

Plugin para OBS Studio que muestra escenas y fuentes **encima de todas tus escenas**, sin copiarlas en cada una. Es un downstream keyer simplificado: una sola lista, un interruptor por overlay y exclusión de escenas con casillas.

*OBS Studio plugin that shows scenes and sources on top of every scene. A simplified downstream keyer: one list, one switch per overlay, and per-scene exclusions with checkboxes.*

## Qué hace

- **Siempre encima:** lo que añades al dock (una escena o una fuente) se ve en el programa sobre cualquier escena.
- **Ojo por overlay** y un interruptor general para apagarlos todos.
- **Ocultar aquí:** excluye la escena actual con un clic.
- **Lista de escenas con casillas**, buscador y botones Todas / Ninguna.
- **Escenas nuevas:** cada overlay decide si aparece o no en las escenas que crees después.
- La configuración se guarda dentro de cada colección de escenas.

## Uso

1. Abre el dock en **Docks → AC Global Overlay**.
2. Pulsa **Añadir escena o fuente**.
3. Pulsa la etiqueta de la derecha de cada fila para elegir en qué escenas se muestra.

Para ordenar las capas arrastra las filas, o usa clic derecho → Subir / Bajar. La primera fila es la capa superior.

### Posición de las fuentes

- Una **escena** usada como overlay ocupa todo el lienzo: edítala como cualquier escena.
- Una **fuente** suelta toma la posición y el tamaño que ya tiene en tu escena actual (o en la primera escena donde exista). Colócala primero y añádela después.

## Limitaciones conocidas

- Los overlays se muestran y ocultan por corte, sin fundido.
- Los overlays quedan por encima de las transiciones tipo stinger.
- En modo estudio se ven en el programa, no en la vista previa.
- Usa el canal de salida 56 de OBS.

## Instalación

Descarga el archivo de tu sistema desde [Releases](../../releases).

- **macOS:** ejecuta el `.pkg`, o descomprime el `.tar.xz` y copia `ac-global-overlay.plugin` en `~/Library/Application Support/obs-studio/plugins/`.
- **Windows:** ejecuta el instalador o descomprime el `.zip` en la carpeta de OBS.
- **Ubuntu:** instala el `.deb`.

Requiere OBS Studio 31.1 o superior.

## Licencia

GPL-2.0-or-later. Basado en [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
