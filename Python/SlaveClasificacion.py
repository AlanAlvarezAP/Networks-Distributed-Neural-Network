import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import numpy as np
import pandas as pd
import socket
import pickle

#  CONFIGURACIÓN  (debe coincidir con el maestro Python)
input_dim   = 200      # flexible - igual que en basicClasificacion.py
num_classes = 3
batch_size  = 1
SLAVE_HOST  = '0.0.0.0'
SLAVE_PORT  = 6000     # cada esclavo escucha en su puerto (6001, 6002, ...)

#  RED NEURONAL  - misma arquitectura que el maestro (4 capas)
#  input(200) → hidden1(128) → hidden2(64) → hidden3(32) → output
class MulticlassClassifier(nn.Module):
    def __init__(self, input_dim: int, num_classes: int,
                 hidden1: int = 128, hidden2: int = 64, hidden3: int = 32):
        super(MulticlassClassifier, self).__init__()
        self.fc1 = nn.Linear(input_dim, hidden1)          # Capa 1
        self.fc2 = nn.Linear(hidden1,   hidden2)           # Capa 2
        self.fc3 = nn.Linear(hidden2,   hidden3)           # Capa 3
        self.class_logits   = nn.Linear(hidden3, num_classes)   # Capa 4 - salida
        self.class_log_vars = nn.Linear(hidden3, num_classes)

    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = F.relu(self.fc3(x))
        logits   = self.class_logits(x)
        log_vars = self.class_log_vars(x)
        return logits, log_vars

#  FUNCIONES DE MATRICES
def get_weight_matrices(model):
    """Extrae matrices de pesos de todas las capas → dict numpy."""
    return {
        'fc1':          model.fc1.weight.data.cpu().numpy().copy(),
        'fc2':          model.fc2.weight.data.cpu().numpy().copy(),
        'fc3':          model.fc3.weight.data.cpu().numpy().copy(),
        'class_logits': model.class_logits.weight.data.cpu().numpy().copy(),
    }

def set_weight_matrices(model, weights_dict):
    """Carga matrices (numpy) en todas las capas del modelo."""
    with torch.no_grad():
        model.fc1.weight.data.copy_(
            torch.from_numpy(weights_dict['fc1'].astype(np.float32)))
        model.fc2.weight.data.copy_(
            torch.from_numpy(weights_dict['fc2'].astype(np.float32)))
        model.fc3.weight.data.copy_(
            torch.from_numpy(weights_dict['fc3'].astype(np.float32)))
        model.class_logits.weight.data.copy_(
            torch.from_numpy(weights_dict['class_logits'].astype(np.float32)))

#  HELPER: recibir bytes exactos del socket
def recv_exact(conn, size):
    data = b''
    while len(data) < size:
        chunk = conn.recv(min(4096, size - len(data)))
        if not chunk:
            break
        data += chunk
    return data

#  CARGA DE DATOS LOCAL DEL ESCLAVO
csv_path = "D:/UCSP/Septimo Semestre/Redes y Comunicacion/ProyectoRedes/DatasetofDiabetes.csv"
df          = pd.read_csv(csv_path, header=None, skiprows=1)
X_np        = df.iloc[:, :input_dim].values.astype(np.float32)
y_onehot_np = df.iloc[:, -num_classes:].values.astype(np.float32)

X       = torch.tensor(X_np)
y       = torch.tensor(y_onehot_np)
dataset = TensorDataset(X, y)

# DataLoader local del esclavo
train_loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)

#  MODELO Y OPTIMIZADOR
model     = MulticlassClassifier(input_dim=input_dim, num_classes=num_classes)
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=0.001)

#  LOOP PRINCIPAL DEL ESCLAVO 

print(f"[ESCLAVO] Escuchando en {SLAVE_HOST}:{SLAVE_PORT} ...")

with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as server:
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((SLAVE_HOST, SLAVE_PORT))
    server.listen(1)

    while True:
        conn, addr = server.accept()
        with conn:
            print(f"[ESCLAVO] Conexión recibida de {addr}")

            # ── M = Calculador() → recibir matrices del maestro C++ ──
            size_bytes = recv_exact(conn, 8)
            size       = int.from_bytes(size_bytes, 'big')
            raw        = recv_exact(conn, size)
            weights_from_master = pickle.loads(raw)
            print(f"[ESCLAVO] Matrices recibidas:"
                  f" fc1{weights_from_master['fc1'].shape}"
                  f" fc2{weights_from_master['fc2'].shape}"
                  f" fc3{weights_from_master['fc3'].shape}"
                  f" logits{weights_from_master['class_logits'].shape}")

            # ── #Load M to NN → cargar matrices al modelo ──
            set_weight_matrices(model, weights_from_master)

            # ── For b_x, b_y in loader → entrenar 1 época local ──
            model.train()
            epoch_loss = 0

            for batch_x, batch_y in train_loader:
                optimizer.zero_grad()

                # model(b_x) → forward pass
                logits, log_vars = model(batch_x)

                # loss + backpropagation
                loss = criterion(logits, batch_y)
                loss.backward()
                optimizer.step()
                epoch_loss += loss.item()

            print(f"[ESCLAVO] Entrenamiento local - Loss: {epoch_loss/len(train_loader):.4f}")

            # ── calculad_std(m) → calcular y devolver matrices al maestro C++ ──
            updated_weights = get_weight_matrices(model)
            response = pickle.dumps(updated_weights)
            conn.sendall(len(response).to_bytes(8, 'big'))
            conn.sendall(response)
            print("[ESCLAVO] Matrices actualizadas enviadas al maestro C++")