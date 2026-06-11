import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import numpy as np
import pandas as pd
import socket
import pickle
import os

# CONFIGURACIÓN
CSV_PATH      = "Dataset of Diabetes .csv"
INPUT_DIM     = 200          # cantidad de features de entrada
NUM_CLASSES   = 3            # clases de salida (columnas del one-hot)
BATCH_SIZE    = 32
NUM_EPOCHS    = 10
LEARNING_RATE = 0.001

SLAVE_HOSTS = [
    ('127.0.0.1', 6001),
    ('127.0.0.1', 6002),
]
N_SLAVES = len(SLAVE_HOSTS)

# PUNTO DE CONEXIÓN CON C++
# El maestro Python se conecta a este host/puerto al final de cada época.
# El programa C++ debe estar escuchando aquí antes de que empiece el entrenamiento.
CPP_MASTER_HOST = '127.0.0.1'
CPP_MASTER_PORT = 7000

# Carpeta compartida donde Python escribe los CSV para C++ y C++ escribe los CSV de vuelta.
# Esta carpeta debe existir y ser accesible tanto por Python como por C++.
CSV_DIR = "matrices_csv"
os.makedirs(CSV_DIR, exist_ok=True)

# Nombres de las 4 capas cuyas matrices de pesos se intercambian con C++.
LAYER_NAMES = ['fc1', 'fc2', 'fc3', 'class_logits']

# RED NEURONAL
class MulticlassClassifier(nn.Module):
    # Define la arquitectura: fc1(200→128), fc2(128→64), fc3(64→32), class_logits(32→3).
    def __init__(self, input_dim: int, num_classes: int,
                 hidden1: int = 128, hidden2: int = 64, hidden3: int = 32):
        super(MulticlassClassifier, self).__init__()
        self.fc1          = nn.Linear(input_dim, hidden1)
        self.fc2          = nn.Linear(hidden1, hidden2)
        self.fc3          = nn.Linear(hidden2, hidden3)
        self.class_logits = nn.Linear(hidden3, num_classes)
        self.class_log_vars = nn.Linear(hidden3, num_classes)

    # Ejecuta el forward pass: ReLU en las 3 primeras capas, luego devuelve logits y log_vars.
    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = F.relu(self.fc3(x))
        logits   = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars

# FUNCIONES DE MATRICES
# Extrae las matrices de pesos de las 4 capas del modelo como diccionario {nombre: numpy array}.
def get_weight_matrices(model):
    return {name: getattr(model, name).weight.data.cpu().numpy().copy()
            for name in LAYER_NAMES}

# Carga un diccionario {nombre: numpy array} como nuevos pesos en las 4 capas del modelo.
def set_weight_matrices(model, weights_dict):
    with torch.no_grad():
        for name, w in weights_dict.items():
            getattr(model, name).weight.data.copy_(
                torch.from_numpy(w.astype(np.float32)))

# Recibe una lista de diccionarios de pesos y devuelve su promedio elemento a elemento.
def average_weight_matrices(list_of_dicts):
    averaged = {}
    for name in LAYER_NAMES:
        stacked = np.stack([d[name] for d in list_of_dicts], axis=0)
        averaged[name] = stacked.mean(axis=0)
    return averaged

# GUARDAR Y CARGAR MATRICES COMO CSV
# Guarda las 4 matrices en archivos CSV dentro de matrices_csv/ con el prefijo dado.
# C++ debe LEER los archivos que genera esta función (prefijo "para_cpp").
# Formato: sin encabezado, valores float32 separados por comas.
# Shapes esperados: fc1(128×200), fc2(64×128), fc3(32×64), class_logits(3×32).
def save_matrices_to_csv(weights_dict, prefix="matriz"):
    for name, matrix in weights_dict.items():
        path = os.path.join(CSV_DIR, f"{prefix}_{name}.csv")
        pd.DataFrame(matrix).to_csv(path, index=False, header=False)
        print(f"  [CSV] Guardado: {path}  shape={matrix.shape}")

# Lee los 4 archivos CSV con el prefijo dado y devuelve un diccionario {nombre: numpy array}.
# C++ debe ESCRIBIR los archivos que lee esta función (prefijo "de_cpp").
# Mismo formato que save_matrices_to_csv: sin encabezado, valores float32.
def load_matrices_from_csv(prefix="matriz"):
    weights_dict = {}
    for name in LAYER_NAMES:
        path = os.path.join(CSV_DIR, f"{prefix}_{name}.csv")
        weights_dict[name] = pd.read_csv(path, header=None).values.astype(np.float32)
        print(f"  [CSV] Cargado:  {path}  shape={weights_dict[name].shape}")
    return weights_dict

# COMUNICACIÓN CON ESCLAVOS PYTHON
# NO son relevantes para la implementación C++.

# Lee exactamente 'size' bytes del socket, acumulando chunks hasta completar.
def recv_exact(conn, size):
    data = b''
    while len(data) < size:
        chunk = conn.recv(min(4096, size - len(data)))
        if not chunk:
            break
        data += chunk
    return data

# Serializa un objeto Python con pickle y lo envía por el socket precedido de su tamaño en 8 bytes.
def send_pickle(conn, obj):
    payload = pickle.dumps(obj)
    conn.sendall(len(payload).to_bytes(8, 'big'))
    conn.sendall(payload)

# Lee 8 bytes del socket para obtener el tamaño y luego deserializa el objeto pickle recibido.
def recv_pickle(conn):
    size = int.from_bytes(recv_exact(conn, 8), 'big')
    return pickle.loads(recv_exact(conn, size))

# Abre conexión TCP con un esclavo Python, le envía el paquete y devuelve los pesos actualizados.
def send_to_slave(host, port, package):
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        send_pickle(s, package)
        updated = recv_pickle(s)
    return updated

# COMUNICACIÓN CON MAESTRO C++
# Función principal de integración con C++. Se llama una vez por época.
# Flujo completo:
#   1. Llama a save_matrices_to_csv() → escribe 4 archivos "matrices_csv/para_cpp_*.csv"
#   2. Abre socket TCP a CPP_MASTER_HOST:CPP_MASTER_PORT y envía el string b"READY" (5 bytes)
#   3. Espera recibir b"DONE" (4 bytes) del proceso C++ como confirmación
#   4. Llama a load_matrices_from_csv() → lee 4 archivos "matrices_csv/de_cpp_*.csv"
#   5. Devuelve las matrices actualizadas para cargarlas en el modelo
#
# Lo que C++ debe hacer al recibir "READY":
#   - Leer los 4 CSV "para_cpp_*.csv"
#   - Aplicar su propia lógica (promedio con esclavos C++, u otro procesamiento)
#   - Escribir los 4 CSV "de_cpp_*.csv" con los pesos resultantes
#   - Responder b"DONE" por el mismo socket
def exchange_with_cpp_master(weights_dict):
    # 1. Guardar matrices como CSV para que el maestro C++ las lea
    save_matrices_to_csv(weights_dict, prefix="para_cpp")

    # 2. Avisar al maestro C++ que los CSV están listos (envía "READY", espera "DONE")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((CPP_MASTER_HOST, CPP_MASTER_PORT))
        s.sendall(b"READY")
        ack = s.recv(4)
        if ack != b"DONE":
            raise RuntimeError(f"Respuesta inesperada del maestro C++: {ack}")

    # 3. Cargar las matrices que el maestro C++ dejó en los CSV "de_cpp_*.csv"
    updated = load_matrices_from_csv(prefix="de_cpp")
    return updated

# CARGA DE DATOS
df          = pd.read_csv(CSV_PATH, header=None, skiprows=1)
X_np        = df.iloc[:, :INPUT_DIM].values.astype(np.float32)
y_onehot_np = df.iloc[:, -NUM_CLASSES:].values.astype(np.float32)

X = torch.tensor(X_np)
y = torch.tensor(y_onehot_np)
print(f"[MAESTRO] Dataset: X{X.shape}  y{y.shape}")

# Divide los datos en partes iguales para enviar una porción a cada esclavo Python.
X_parts = np.array_split(X_np, N_SLAVES)
y_parts = np.array_split(y_onehot_np, N_SLAVES)

# MODELO Y OPTIMIZADO
model     = MulticlassClassifier(input_dim=INPUT_DIM, num_classes=NUM_CLASSES)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)

# DataLoader completo para el entrenamiento local del maestro.
dataset      = TensorDataset(X, y)
train_loader = DataLoader(dataset, batch_size=BATCH_SIZE, shuffle=True)

# LOOP PRINCIPAL
# En cada época el flujo es:
#   1. El maestro entrena localmente (backprop completo sobre todos los datos)
#   2. Extrae las 4 matrices de pesos resultantes
#   3. Las envía a cada esclavo Python junto con su porción de datos
#   4. Cada esclavo entrena una época y devuelve sus pesos actualizados
#   5. El maestro promedia los pesos de todos los esclavos que respondieron
#   6. Guarda esos pesos en CSV y se los pasa al maestro C++ via socket
#   7. El maestro C++ hace su procesamiento, escribe nuevos CSV y responde "DONE"
#   8. El maestro Python carga esos CSV y actualiza el modelo para la siguiente época
print(f"[MAESTRO] Iniciando entrenamiento — {NUM_EPOCHS} épocas\n")

for epoch in range(1, NUM_EPOCHS + 1):
    print(f"{'─'*55}")
    print(f"[MAESTRO] Época {epoch}/{NUM_EPOCHS}")

    # 1. Entrenamiento local del maestro: forward + backprop sobre todos los datos
    model.train()
    epoch_loss = 0
    for batch_x, batch_y in train_loader:
        optimizer.zero_grad()
        logits, log_vars = model(batch_x)
        loss = criterion(logits, batch_y)
        loss.backward()
        optimizer.step()
        epoch_loss += loss.item()
    print(f"  [MAESTRO] Loss local: {epoch_loss/len(train_loader):.4f}")

    # 2. Extraer las 4 matrices de pesos actuales del modelo─
    current_weights = get_weight_matrices(model)

    # 3. Enviar pesos + datos a cada esclavo Python y recolectar sus pesos ─
    updated_list = []
    for idx, (host, port) in enumerate(SLAVE_HOSTS):
        package = {
            'weights': current_weights,
            'X': X_parts[idx],
            'y': y_parts[idx],
        }
        print(f"  → Enviando a esclavo {idx+1} ({host}:{port})")
        try:
            updated = send_to_slave(host, port, package)
            updated_list.append(updated)
            print(f"  ← Matrices recibidas del esclavo {idx+1}")
        except Exception as e:
            print(f"  [ERROR] Esclavo {idx+1}: {e}")

    # 4. Promediar los pesos recibidos de los esclavos Python
    if updated_list:
        averaged_weights = average_weight_matrices(updated_list)
        set_weight_matrices(model, averaged_weights)
        print(f"  [MAESTRO] Matrices promediadas de {len(updated_list)} esclavo(s)")
    else:
        averaged_weights = current_weights
        print(f"  [AVISO] Sin respuesta de esclavos, se usan los pesos locales")

    # 5. Intercambio con el maestro C++: guardar CSV → señal "READY" → esperar "DONE" → cargar CSV
    print(f"  → Guardando matrices como CSV y contactando Maestro C++...")
    try:
        cpp_updated = exchange_with_cpp_master(averaged_weights)
        set_weight_matrices(model, cpp_updated)
        print(f"  ← Maestro C++ respondió — modelo actualizado")
    except Exception as e:
        print(f"  [AVISO] Maestro C++ no disponible: {e}")
        print(f"          Se conservan las matrices promediadas")

# RESULTADOS FINALES
print(f"\n{'═'*55}")
print("[MAESTRO] Entrenamiento finalizado.")
for name in LAYER_NAMES:
    w = getattr(model, name).weight.data
    print(f"  {name}: shape={tuple(w.shape)}  mean={w.mean():.6f}  std={w.std():.6f}")
