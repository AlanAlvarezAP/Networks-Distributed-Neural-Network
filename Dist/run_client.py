import time

import mi_cliente_udp

print("[Python] Instanciando el Cliente de C++...")
client = mi_cliente_udp.PyClient()

print("[Python] Levantando hilos de lectura y timeouts...")
client.iniciar_hilos()

print("[Python] Lanzando acción de Login...")

client.ejecutar_accion("L")

try:
    while client.esta_corriendo():
        if client.esta_logeado():
            print("[Python Status] Estado actual: Logeado en el servidor.")
        time.sleep(2)
except KeyboardInterrupt:
    print("\n[Python] Saliendo y enviando Logout...")
    client.ejecutar_accion("O")
    client.cerrar()
