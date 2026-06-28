import sys
import time

import mi_servidor_udp


def mostrar_menu():
    print("===================================")
    print("|          Welcome to             |")
    print("|      Matrix NN distribution     |")
    print("|                                 |")
    print("|  1. Load Matrix                 |")
    print("|  2. Show Clients                |")
    print("|  3. Exit                        |")
    print("===================================")


print("[Python] Instanciando el Servidor de C++...")
server = mi_servidor_udp.PyServer()

print("[Python] Iniciando sockets e hilos de escucha...")
server.iniciar_servidor()

try:
    while True:
        mostrar_menu()
        opcion = input("SELECT AN ACTION :D -> ").strip()

        if opcion == "1":
            total_clientes = server.cantidad_clientes()
            if total_clientes < 3:
                print(
                    f"\n[WARNING] Need more clients !!! (Activos actualmente: {total_clientes})\n"
                )
                # time.sleep(1)
                continue

            print("\n[Python] Cediendo control a C++ para leer la ruta del CSV...")
            server.cargar_matriz_csv()
            print("[Python] Matriz procesada y enviada.\n")

        elif opcion == "2":
            print("\n[Python Status] Mostrando clientes conectados:")
            server.mostrar_clientes()
            print()

        elif opcion == "3":
            print("\n[Python] Apagando el servidor...")
            server.cerrar()
            break

        else:
            print("\n[Invalid option] Intenta de nuevo.\n")
            time.sleep(1)

except KeyboardInterrupt:
    print("\n\n[Python] Interrupción de teclado detectada. Apagando el servidor...")
    server.cerrar()
