#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
Integración del cliente UDP con la lógica del esclavo (entrenamiento por batch).
Combina run_client.py y esclavo_prueba.py en un único script coherente.
"""

import os
import select
import sys
import time

# ============================================================================
# 1. Importar el módulo C++ que implementa el cliente UDP
# ============================================================================
import mi_cliente_udp
import numpy as np
import pandas as pd
import torch
import torch.nn as nn
import torch.nn.functional as F

# ============================================================================
# 2. Configuración del modelo y funciones de entrenamiento (esclavo_prueba.py)
# ============================================================================
INPUT_DIM = 11
NUM_CLASSES = 3
BATCH_SIZE = 32
LEARNING_RATE = 0.001

LAYER_NAMES = ["fc1", "fc2", "fc3", "class_logits"]
FLOAT_PRECISION = 6


class MulticlassClassifier(nn.Module):
    """Modelo de clasificación multiclase (idéntico al del maestro)."""

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


def csv_to_weights(model, path: str):
    """Carga los pesos del modelo desde un CSV (formato: layer,row,col,value)."""
    df = pd.read_csv(path)
    with torch.no_grad():
        for name in LAYER_NAMES:
            sub = df[df["layer"] == name].sort_values(["row", "col"])
            rows = sub["row"].max() + 1
            cols = sub["col"].max() + 1
            matrix = sub["value"].values.astype(np.float32).reshape(rows, cols)
            getattr(model, name).weight.data.copy_(torch.from_numpy(matrix))
            print(f"  [csv_to_weights] capa={name}  shape=({rows},{cols}) cargado")


def train_batch(model, X_np: np.ndarray, y_np: np.ndarray, criterion, optimizer):
    """Entrena una sola iteración con un batch de datos."""
    model.train()
    bx = torch.from_numpy(X_np)
    by = torch.from_numpy(y_np)
    optimizer.zero_grad()
    logits, _ = model(bx)
    loss = criterion(logits, by)
    loss.backward()
    optimizer.step()
    return loss.item()


def weights_to_csv(model, path: str) -> str:
    """Exporta todos los pesos del modelo a un CSV único."""
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


# ============================================================================
# 3. Instancias globales del modelo y optimizador (persisten entre rondas)
# ============================================================================
model = MulticlassClassifier()
criterion = nn.CrossEntropyLoss()
optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)


def procesar_ronda_esclavo(
    pesos_entrada: str, batch_csv: str, pesos_salida: str
) -> str:
    """
    Ejecuta una ronda completa del esclavo:
      1. Carga los pesos desde 'pesos_entrada'.
      2. Lee el batch de datos desde 'batch_csv' (debe contener X e y).
      3. Entrena una iteración.
      4. Exporta los pesos actualizados a 'pesos_salida'.
    Retorna la ruta del archivo de salida.
    """
    print("\n[ESCLAVO] Iniciando ronda de entrenamiento...")

    # 1. Cargar pesos
    print("[ESCLAVO] Cargando pesos desde:", pesos_entrada)
    csv_to_weights(model, pesos_entrada)

    # 2. Leer batch de datos (se espera un CSV con columnas: features... y las últimas NUM_CLASSES son one-hot)
    #    Para simplicidad, si el archivo no existe, generamos datos sintéticos (como en esclavo_prueba.py)
    if os.path.exists(batch_csv):
        df_batch = pd.read_csv(batch_csv)
        X_np = df_batch.iloc[:, :INPUT_DIM].values.astype(np.float32)
        y_np = df_batch.iloc[:, -NUM_CLASSES:].values.astype(np.float32)
        print(f"[ESCLAVO] Batch real cargado: {X_np.shape}")
    else:
        print(f"[ESCLAVO] {batch_csv} no encontrado. Usando batch sintético.")
        np.random.seed(7)  # Para reproducibilidad
        X_np = np.random.randn(BATCH_SIZE, INPUT_DIM).astype(np.float32)
        y_np = np.eye(NUM_CLASSES)[
            np.random.randint(0, NUM_CLASSES, BATCH_SIZE)
        ].astype(np.float32)

    # 3. Entrenar
    loss = train_batch(model, X_np, y_np, criterion, optimizer)
    print(f"[ESCLAVO] Batch entrenado  loss={loss:.4f}")

    # 4. Exportar pesos actualizados
    print("[ESCLAVO] Exportando pesos actualizados:")
    weights_to_csv(model, pesos_salida)
    print(f"  → {pesos_salida} listo para enviar al servidor\n")
    return pesos_salida


# ============================================================================
# 4. Cliente principal (basado en run_client.py)
# ============================================================================
def mostrar_menu():
    print("============================================")
    print("|    Cliente Esclavo - NN distribuida       |")
    print("|                                           |")
    print("|  1. Login                                 |")
    print("|  2. Logout                                |")
    print("============================================")


def main():
    print("[Python] Instanciando el Cliente de C++...")
    client = mi_cliente_udp.PyClient()

    print("[Python] Levantando hilos de lectura y timeouts...")
    client.iniciar_hilos()

    mostrar_menu()
    print("SELECT AN ACTION :D (or wait for incoming Matrix Data)")

    try:
        while client.esta_corriendo():
            # 1. Verificar si llegaron datos del servidor
            if client.tiene_datos_nuevos():
                print("\n[Python] ¡Detectados nuevos datos del servidor!")

                # Rutas de archivos utilizados en la comunicación
                PESOS_ENTRADA = f"{client.obtener_nombre()}_weight_sc.csv"  # Lo escribe el servidor (o simulación)
                BATCH_CSV = f"{client.obtener_nombre()}_batch_sc.csv"  # Datos de entrenamiento enviados por el servidor
                PESOS_SALIDA = f"{client.obtener_nombre()}_weight_cs.csv"  # Lo generamos y enviamos al servidor

                # Ejecutar una ronda completa del esclavo
                # (En un sistema real, el servidor habrá escrito los archivos de entrada)
                archivo_salida = procesar_ronda_esclavo(
                    pesos_entrada=PESOS_ENTRADA,
                    batch_csv=BATCH_CSV,
                    pesos_salida=PESOS_SALIDA,
                )

                # Enviar el archivo de resultados al servidor
                print(f"[Python] Enviando resultado '{archivo_salida}' al servidor...")
                client.enviar_resultado_csv(archivo_salida)

                client.reset_datos_nuevos()
                print("[Python] Flujo completado. Volviendo al menú.\n")
                mostrar_menu()
                print("SELECT AN ACTION :D (or wait for incoming Matrix Data)")

            # 2. Entrada no bloqueante (espera hasta 0.5 segundos)
            rlist, _, _ = select.select([sys.stdin], [], [], 0.5)
            if rlist:
                opcion = sys.stdin.readline().strip()

                if opcion == "1":
                    client.ejecutar_accion("L")  # Login
                elif opcion == "2":
                    client.ejecutar_accion("O")  # Logout
                    client.cerrar()
                    break
                else:
                    client.ejecutar_accion("z")  # Opción inválida

    except KeyboardInterrupt:
        print("\n\n[Python] Interrupción detectada. Saliendo de forma segura...")
        if client.esta_corriendo():
            client.ejecutar_accion("O")
            client.cerrar()


if __name__ == "__main__":
    main()
