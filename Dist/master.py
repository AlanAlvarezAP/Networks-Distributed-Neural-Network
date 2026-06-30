#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import time

# ============================================================================
# 1. Importar el módulo C++ que implementa el servidor UDP
# ============================================================================
import mi_servidor_udp
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset

# ============================================================================
# 2. Configuración del modelo y entrenamiento (tomado de maestro_prueba.py)
# ============================================================================
CSV_PATH = "Dataset of Diabetes.csv"  # Ruta fija al dataset
INPUT_DIM = 11
NUM_CLASSES = 3
BATCH_SIZE = 32
NUM_EPOCHS = 10
LEARNING_RATE = 0.001

LAYER_NAMES = ["fc1", "fc2", "fc3", "class_logits"]
FLOAT_PRECISION = 6


class MulticlassClassifier(nn.Module):
    """Modelo de clasificación multiclase con 4 capas lineales."""

    def __init__(
        self, input_dim=INPUT_DIM, num_classes=NUM_CLASSES, h1=128, h2=64, h3=32
    ):
        super().__init__()
        self.fc1 = nn.Linear(input_dim, h1)
        self.fc2 = nn.Linear(h1, h2)
        self.fc3 = nn.Linear(h2, h3)
        self.class_logits = nn.Linear(h3, num_classes)
        self.class_log_vars = nn.Linear(h3, num_classes)

    def forward(self, x: torch.Tensor):
        x = F.relu(self.fc1(x))
        x = F.relu(self.fc2(x))
        x = F.relu(self.fc3(x))
        return self.class_logits(x), self.class_log_vars(x)


def train_epoch(model, loader, criterion, optimizer):
    """Entrena una época completa."""
    model.train()
    total = 0.0
    for bx, by in loader:
        optimizer.zero_grad()
        logits, _ = model(bx)
        loss = criterion(logits, by)
        loss.backward()
        optimizer.step()
        total += loss.item()
    return total / len(loader)


def train_full(model, loader, criterion, optimizer, num_epochs=NUM_EPOCHS):
    """Entrenamiento completo del modelo."""
    print(f"[MAESTRO] Entrenamiento inicial — {num_epochs} épocas")
    losses = []
    for ep in range(1, num_epochs + 1):
        loss = train_epoch(model, loader, criterion, optimizer)
        losses.append(loss)
        print(f"  Época {ep:2d}/{num_epochs}  loss={loss:.4f}")
    print("[MAESTRO] Entrenamiento completado.\n")
    return losses


def weights_to_csv(model, path: str = "pesos_maestro.csv") -> str:
    """Exporta todos los pesos del modelo a un único CSV."""
    records = []
    for name in LAYER_NAMES:
        matrix = getattr(model, name).weight.data.cpu().numpy()
        rows, cols = matrix.shape
        for r in range(rows):
            for c in range(cols):
                records.append(
                    (name, r, c, round(float(matrix[r, c]), FLOAT_PRECISION))
                )
    df = pd.DataFrame(records, columns=["layer", "row", "col", "value"])
    df.to_csv(path, index=False)
    print(
        f"  [weights_to_csv] {path}  ({len(records)} valores, {len(LAYER_NAMES)} capas)"
    )
    return path


def entrenar_maestro_y_exportar(dataset_path=None, weights_output_path="weights.csv"):

    # Si no se pasa un dataset_path específico, usamos la constante global por defecto
    if dataset_path is None:
        dataset_path = CSV_PATH

    # Cargar datos desde la ruta seleccionada
    if os.path.exists(dataset_path):
        df = pd.read_csv(dataset_path, header=None, skiprows=1)
        X_np = df.iloc[:, :INPUT_DIM].values.astype(np.float32)
        y_onehot_np = df.iloc[:, -NUM_CLASSES:].values.astype(np.float32)
        print(f"[MAESTRO] Dataset cargado desde '{dataset_path}': {X_np.shape}")
    else:
        print(
            f"[MAESTRO] CSV '{dataset_path}' no encontrado — usando datos sintéticos (500 muestras)"
        )
        np.random.seed(42)
        X_np = np.random.randn(500, INPUT_DIM).astype(np.float32)
        y_onehot_np = np.eye(NUM_CLASSES)[
            np.random.randint(0, NUM_CLASSES, 500)
        ].astype(np.float32)

    loader = DataLoader(
        TensorDataset(torch.tensor(X_np), torch.tensor(y_onehot_np)),
        batch_size=BATCH_SIZE,
        shuffle=True,
    )

    # Instanciar modelo y optimizador
    model = MulticlassClassifier()
    criterion = nn.CrossEntropyLoss()
    optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)

    # Entrenar
    train_full(model, loader, criterion, optimizer, num_epochs=NUM_EPOCHS)

    # Exportar pesos al archivo personalizado
    print(f"[MAESTRO] Exportando pesos a CSV para C++ en '{weights_output_path}':")
    out_path = weights_to_csv(model, path=weights_output_path)
    print(f"  (C++ podrá leer {out_path} para distribuirlos a los esclavos)\n")

    return model


# ============================================================================
# 3. Funciones del menú (tomadas y adaptadas de run_server.py)
# ============================================================================
def mostrar_menu():
    print("============================================")
    print("|    Servidor + Maestro de NN distribuida   |")
    print("|                                           |")
    print("|  1. Cargar Matriz y Entrenar Maestro      |")
    print("|  2. Mostrar Clientes                      |")
    print("|  3. Salir                                |")
    print("============================================")


# ============================================================================
# 4. Programa principal
# ============================================================================
def main():
    print("[Python] Instanciando el servidor C++...")
    server = mi_servidor_udp.PyServer()

    print("[Python] Iniciando entrenamiento del modelo maestro...")
    entrenar_maestro_y_exportar()
    print("[Python] Pesos del maestro guardados en 'pesos_maestro.csv'.\n")

    print("[Python] Iniciando sockets e hilos de escucha...")
    server.iniciar_servidor()

    try:
        while True:
            mostrar_menu()
            opcion = input("SELECT AN ACTION :D -> ").strip()

            if opcion == "1":
                total_clientes = server.obtener_cantidad_clientes()
                if total_clientes < 3:
                    print(
                        f"\n[WARNING] Se necesitan al menos 3 clientes. (Activos: {total_clientes})\n"
                    )
                    continue

                # Distribuir la matriz de datos a los clientes (vía C++)
                print(
                    "\n[Python] Cediendo control a C++ para leer y enviar la matriz CSV..."
                )
                server.cargar_matriz_csv()
                print("[Python] Matriz enviada a los clientes.\n")

                PESOS_SALIDA = "returned_master_weight.csv"  # Lo escribe el servidor (o simulación)
                BATCH_CSV = "master_batch.csv"  # Datos de entrenamiento enviados por el servidor
                entrenar_maestro_y_exportar(BATCH_CSV, PESOS_SALIDA)

            elif opcion == "2":
                print("\n[Python Status] Clientes conectados:")
                server.mostrar_clientes()
                print()

            elif opcion == "3":
                print("\n[Python] Apagando el servidor...")
                server.cerrar()
                break

            else:
                print("\n[Opción inválida] Intenta de nuevo.\n")
                time.sleep(1)

    except KeyboardInterrupt:
        print("\n\n[Python] Interrupción de teclado. Apagando el servidor...")
        server.cerrar()


if __name__ == "__main__":
    main()
