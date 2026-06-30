import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import numpy as np
import socket
import pickle

# Configuración
SLAVE_HOST    = '0.0.0.0'
SLAVE_PORT    = 6001        # cambia a 6002 para la segunda instancia

#Dimensiones
INPUT_DIM     = 200
NUM_CLASSES   = 3
BATCH_SIZE    = 32
LEARNING_RATE = 0.001

# Nombres de las 4 capas cuyos pesos se sincronizan con el maestro Python en cada época.
LAYER_NAMES = ['fc1', 'fc2', 'fc3', 'class_logits']

# Red Neuronal
# Define la misma arquitectura que el maestro: fc1(200→128), fc2(128→64), fc3(64→32), class_logits(32→3).
# Debe ser IDÉNTICA a la del maestro para que los pesos sean compatibles.
class MulticlassClassifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int,
                 hidden1: int = 128, hidden2: int = 64, hidden3: int = 32):
        super(MulticlassClassifier, self).__init__()
        self.fc1          = nn.Linear(input_dim, hidden1)
        self.fc2          = nn.Linear(hidden1, hidden2)
        self.fc3          = nn.Linear(hidden2, hidden3)
        self.class_logits = nn.Linear(hidden3, num_classes)
        self.class_log_vars = nn.Linear(hidden3, num_classes)

    # Ejecuta el forward pass: ReLU en las 3 primeras capas, devuelve logits y log_vars.
    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = F.relu(self.fc3(x))
        logits   = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars

# Funciones de Matrices
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

# Comunicación
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

# Modelo y Optimizado (persisten entre épocas)
# El modelo y el optimizador se crean una sola vez y se reusan en todas las épocas.
# Los pesos se sobreescriben en cada época con los del maestro antes de entrenar.
model     = MulticlassClassifier(input_dim=INPUT_DIM, num_classes=NUM_CLASSES)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)

# Loop principal del esclavo
# El esclavo corre indefinidamente esperando conexiones del maestro Python.
# Por cada conexión (= una época) hace:
#   1. Recibe del maestro: pesos actuales del modelo + porción de datos (X, y)
#   2. Carga esos pesos en su modelo local
#   3. Entrena una época con los datos recibidos (forward + backprop)
#   4. Envía al maestro las 4 matrices de pesos resultantes
# El maestro luego promedia los pesos de todos los esclavos que respondieron.
print(f"[ESCLAVO :{SLAVE_PORT}] Escuchando en {SLAVE_HOST}:{SLAVE_PORT} ...")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((SLAVE_HOST, SLAVE_PORT))
    server.listen(5)

    epoch_counter = 0
    while True:
        conn, addr = server.accept()
        with conn:
            epoch_counter += 1
            print(f"\n[ESCLAVO :{SLAVE_PORT}] Época {epoch_counter} — conexión de {addr}")

            # 1. Recibir paquete del maestro: pesos del modelo + porción de datos
            package             = recv_pickle(conn)
            weights_from_master = package['weights']   # dict {nombre: numpy array}
            X_np                = package['X']          # ndarray float32 shape (N, 200)
            y_np                = package['y']          # ndarray float32 shape (N, 3) one-hot

            print(f"  Datos recibidos: X{X_np.shape}  y{y_np.shape}")

            # 2. Cargar los pesos del maestro en el modelo local─
            set_weight_matrices(model, weights_from_master)

            # 3. Construir DataLoader con los datos recibidos
            X_t      = torch.from_numpy(X_np)
            y_t      = torch.from_numpy(y_np)
            dataset  = TensorDataset(X_t, y_t)
            loader   = DataLoader(dataset, batch_size=BATCH_SIZE, shuffle=True)

            # 4. Entrenar una época: forward pass + backpropagation
            model.train()
            epoch_loss = 0
            for batch_x, batch_y in loader:
                optimizer.zero_grad()
                logits, log_vars = model(batch_x)
                loss = criterion(logits, batch_y)
                loss.backward()
                optimizer.step()
                epoch_loss += loss.item()

            print(f"  Loss local: {epoch_loss/len(loader):.4f}")

            # 5. Extraer pesos actualizados y enviarlos de vuelta al maestro
            # El maestro recibirá estos pesos, los promediará con los de los
            # demás esclavos y luego los pasará al maestro C++ via CSV.
            updated_weights = get_weight_matrices(model)
            send_pickle(conn, updated_weights)
            print(f"  [ESCLAVO :{SLAVE_PORT}] 4 matrices enviadas al Maestro Python")
