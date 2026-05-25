import numpy as np
import calculator

import torch
import torch.nn as nn
import numpy as np

#  RED NEURONAL DE PRUEBA  - 4 capas (consistente con basicClasificacion.py)
class SimpleNN(nn.Module):
    def __init__(self):
        super(SimpleNN, self).__init__()
        self.fc1 = nn.Linear(4, 3)   # Capa 1: weight shape [3, 4]
        self.fc2 = nn.Linear(3, 3)   # Capa 2: weight shape [3, 3]  ← NUEVA
        self.fc3 = nn.Linear(3, 2)   # Capa 3: weight shape [2, 3]  ← NUEVA
        self.fc4 = nn.Linear(2, 2)   # Capa 4: weight shape [2, 2]  ← NUEVA

    def forward(self, x):
        x = self.fc1(x)
        x = self.fc2(x)
        x = self.fc3(x)
        return self.fc4(x)

model = SimpleNN()

#  EXTRAE MATRICES DE TODAS LAS CAPAS  (líneas 18-22 originales)
#  Corrige variables relacionadas con matrices (nombres consistentes)
layer_names = ['fc1', 'fc2', 'fc3', 'fc4']
original_matrices = {}

for layer_name in layer_names:
    layer  = getattr(model, layer_name)
    matrix = np.asarray(layer.weight.data.cpu().numpy(), dtype=np.float64)
    original_matrices[layer_name] = matrix
    print(f"Original {layer_name} weights (np.matrix):\n",
          np.matrix(matrix), "\n")

#  DISTRIBUIR MATRICES  (línea 18: distribuir a los 10 esclavos)
#  Simula: enviar cada matriz a 10 esclavos → recibir promedio
updated_matrices = {}

for layer_name in layer_names:
    a = original_matrices[layer_name]           # matriz original de la capa

    # calculator.matrix_add simula la operación distribuida:
    # en producción aquí se envía 'a' a 10 esclavos y se recibe la media
    n = calculator.matrix_add(a, a)             # resultado distribuido

    updated_matrices[layer_name] = np.asarray(n, dtype=np.float32)
    print(f"Updated {layer_name} weights:\n",
          np.matrix(updated_matrices[layer_name]), "\n")

#  CARGAR MATRICES ACTUALIZADAS AL MODELO  (líneas 21-22 corregidas)
with torch.no_grad():
    for layer_name in layer_names:
        layer = getattr(model, layer_name)
        layer.weight.data.copy_(
            torch.from_numpy(updated_matrices[layer_name])
        )

print("Todos los pesos actualizados correctamente:")
for layer_name in layer_names:
    layer = getattr(model, layer_name)
    print(f"  {layer_name}: {layer.weight.data}")