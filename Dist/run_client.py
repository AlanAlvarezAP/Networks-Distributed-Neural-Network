import sys
import time

import mi_cliente_udp


def mostrar_menu():
    print("===================================")
    print("|          Welcome to             |")
    print("|      Matrix NN distribution     |")
    print("|                                 |")
    print("|  1. Login                       |")
    print("|  2. Logout                      |")
    print("===================================")


print("[Python] Instanciando el Cliente de C++...")
client = mi_cliente_udp.PyClient()

print("[Python] Levantando hilos de lectura y timeouts...")
client.iniciar_hilos()

mostrar_menu()

try:
    while client.esta_corriendo():
        print("SELECT AN ACTION :D ")
        opcion = input().strip()

        if opcion == "1":
            client.ejecutar_accion("L")

        elif opcion == "2":
            client.ejecutar_accion("O")
            client.cerrar()
            break
        else:
            client.ejecutar_accion("z")

except KeyboardInterrupt:
    print("\n[Python] Interrupción detectada. Saliendo de forma segura...")
    if client.esta_corriendo():
        client.ejecutar_accion("O")
        client.cerrar()
