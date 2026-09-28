# Guía de Uso: cDockerStats

Bienvenido al manual de usuario de **cDockerStats**. A continuación, encontrarás una descripción paso a paso de cómo aprovechar al máximo todas las vistas y herramientas que te ofrece la interfaz para monitorear tu ecosistema Docker.

---

## 1. Panel de Métricas Globales (Superior)

Al abrir la aplicación, lo primero que notarás en la sección de arriba son los tres medidores radiales. Estos proporcionan una vista panorámica (a nivel del *host*) del costo de tus contenedores:

- **Global CPU**: Mide la suma de todos los ciclos de procesamiento usados por los contenedores activos respecto a tus núcleos.
- **Global Memory**: Compara toda la RAM consumida de los contenedores versus el límite físico total de la placa de tu computadora, no valores virtuales, lo que te garantiza precisión.
- **Global Net I/O**: Refleja la suma de transferencias de descarga/subida de red expresada en Megabytes por segundo.

*Estas métricas se actualizan de manera automática en tiempo real cada 2 segundos.*

---

## 2. Grilla y Estado Individual (Inferior Izquierda)

En el cuadrante inferior izquierdo encontrarás la **Tabla de Contenedores**.

- Enumera todos los contenedores existentes (ya sean encendidos, detenidos o reiniciándose).
- Puedes leer columnas como el ID corto, el Nombre que le diste, el Status actual, y el consumo individual y preciso de sus componentes (CPU %, Memoria límite del contenedor particular, I/O de red, I/O de disco).
- Esta lista mantendrá su propio auto-orden y refrescará sus números conforme lo reporta el daemon de Docker.

---

## 3. Grafo de Topología de Red (Inferior Derecha)

En paralelo a la lista de contenedores, la sección derecha genera una previsualización espacial con el motor `Graphviz`.

- Verás **Nodos**. Cada burbuja central de un color determinado representa una *"Docker Network"* (ej. *bridge*, *host*, o las redes personalizadas creadas por tu `docker-compose`).
- Conectados mediante líneas o aristas, aparecerán los nombres de los contenedores subscritos a esas redes.
- Es excelente para identificar y auditar de un simple vistazo si ciertos backends o bases de datos están comunicándose correctamente sobre redes asiladas o están indebidamente en la red default.

---

## 4. Gestión e Interacción con Contenedores

La grilla de la izquierda es interactiva y te provee herramientas de mantenimiento directo sin tener que abrir una terminal.

### Ver Variables de Entorno (Modal de Inspección)
1. Haz **Doble Clic** sobre la fila de cualquier contenedor en la tabla.
2. Se abrirá una ventana tipo "Modal" superpuesta. 
3. Podrás consultar una recopilación cruda de todas las Variables de Entorno (`ENV`) que le han sido inyectadas a ese contenedor (útil para chequear URIs, contraseñas, configuraciones secretas, puertos).

### Iniciar, Detener o Reiniciar (Menú de Contexto)
1. Selecciona la fila del contenedor de tu interés.
2. Haz **Clic Derecho** sobre ella.
3. Se revelará un pequeño menú emergente de opciones. Al hacer clic en **Start**, **Stop** o **Restart**, la interfaz gráfica enviará la instrucción a Docker silenciosamente y verás los cambios reflejados en la grilla inmediatamente.

---

## Solución de Problemas

- **La aplicación abre pero la grilla aparece vacía**: Esto usualmente significa que la aplicación no tiene los privilegios adecuados para leer los archivos o el socket local de Docker. Asegúrate de ejecutar tu sesión de terminal con un usuario pertenezca al grupo `docker`.
  - Lo puedes solucionar ejecutando en tu terminal: `sudo usermod -aG docker $USER` (requiere que cierres sesión y la vuelvas a iniciar).
- **Lentitud o congelamiento temporal**: Si la tabla de contenedores es gigantesca y tienes cientos en tu máquina, las consultas asíncronas cURL podrían tardar un parpadeo extra. La interfaz continuará siendo responsiva una vez termine la lectura del subproceso en el fondo.
