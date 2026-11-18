# AC Global Overlay

Plugin para OBS Studio que muestra escenas y fuentes **encima de todas tus escenas**, sin copiarlas en cada una. Es un downstream keyer simplificado: una sola lista, un interruptor por overlay y exclusión de escenas con casillas.

*OBS Studio plugin that shows scenes and sources on top of every scene. A simplified downstream keyer: one list, one switch per overlay, and per-scene exclusions with checkboxes.*

## Qué hace

- **Siempre encima:** lo que añades al dock (una escena o una fuente) se ve en el programa sobre cualquier escena.
- **Audio global:** una fuente de solo audio (micrófono, música) también se puede añadir; suena en todas las escenas y respeta las mismas exclusiones y grupos.
- **Ojo por overlay** y un interruptor general para apagarlos todos.
- **Ocultar aquí:** excluye la escena actual con un clic.
- **Excepciones por escena**, plegadas: lista con casillas, buscador y botones Todas / Ninguna.
- **Escenas nuevas:** cada overlay decide si aparece o no en las escenas que crees después.
- **Solo en grupos:** con [AC Scene Groups](https://github.com/alejox/ac-scene-folders) instalado, un overlay puede limitarse a uno o varios grupos de escenas y seguirlos automáticamente. Sin ese plugin la opción no aparece.
- La configuración se guarda dentro de cada colección de escenas.

## Uso

1. Abre el dock en **Docks → AC Global Overlay**.
2. Pulsa **Añadir escena o fuente**.
3. Pulsa la etiqueta de la derecha de cada fila para elegir en qué escenas se muestra.

Para ordenar las capas arrastra las filas, o usa clic derecho → Subir / Bajar. La primera fila es la capa superior.

### Posición de las fuentes

- Una **escena** usada como overlay ocupa todo el lienzo: edítala como cualquier escena.
- Una **fuente** suelta se añade con su propio tamaño, en la posición que ya tiene en tu escena actual.
- **Ir a la fuente** (el botón de mira de cada fila) te lleva a donde se edita y la deja seleccionada. Para las fuentes sueltas abre la escena **Overlays globales**, que el plugin crea la primera vez: lo que muevas o escales ahí cambia en todas las escenas, en vivo. Un aviso en el dock te devuelve a la escena donde estabas.
- Una fuente que añadas directamente a la escena **Overlays globales** se convierte sola en overlay.
- Clic derecho → **Posición rápida**: ajustar a pantalla, centro o una esquina.

## Actualizaciones

Al abrir OBS el plugin consulta la última versión publicada en este repositorio.

- **macOS**, con el plugin en `~/Library/Application Support/obs-studio/plugins/`: la versión nueva se descarga e instala sola y el dock avisa que reinicies OBS.
- **Windows y Ubuntu**, o instalaciones en carpetas del sistema: el dock avisa que hay una versión nueva con un botón para descargarla.

La descarga solo se acepta desde los releases de este repositorio, por HTTPS.

## Limitaciones conocidas

- Los overlays se muestran y ocultan por corte, sin fundido.
- Los overlays quedan por encima de las transiciones tipo stinger.
- En modo estudio se ven en el programa, no en la vista previa.
- Usa el canal de salida 56 de OBS.

## Instalación

Descarga el archivo de tu sistema desde [Releases](../../releases).

Para publicar una versión nueva, sube una rama `release/<versión>` (por ejemplo `release/0.1.0`) o una etiqueta con la versión.

- **macOS:** descomprime el `.tar.xz` y copia `ac-global-overlay.plugin` en `~/Library/Application Support/obs-studio/plugins/`.
- **Windows:** descomprime el `.zip` en la carpeta de OBS.
- **Ubuntu:** instala el `.deb`.

Requiere OBS Studio 31.1 o superior.

## Licencia

GPL-2.0-or-later. Basado en [obs-plugintemplate](https://github.com/obsproject/obs-plugintemplate).
