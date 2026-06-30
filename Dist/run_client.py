import os
import select  # <-- Importamos select para lectura no bloqueante
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
print("SELECT AN ACTION :D (or wait for incoming Matrix Data)")

try:
    while client.esta_corriendo():
        # 1. VERIFICACIÓN ASÍNCRONA: ¿Llegaron datos del servidor?
        if client.tiene_datos_nuevos():
            print("\n[Python] ¡Detectados nuevos datos de matriz del servidor!")

            print("[Python] Procesando datos con la Red Neuronal...")
            time.sleep(2)  # Simulando el tiempo de cómputo

            nombre_archivo_salida = f"{client.obtener_nombre()}_weight.csv"

            if not os.path.exists(nombre_archivo_salida):
                with open(nombre_archivo_salida, "w") as f:
                    f.write("10,20,30,40,50,60")

            print(
                f"[Python] Enviando resultado final '{nombre_archivo_salida}' al servidor..."
            )
            client.enviar_resultado_csv(nombre_archivo_salida)

            client.reset_datos_nuevos()
            print("[Python] Flujo completado. Volviendo al menú.\n")
            mostrar_menu()
            print("SELECT AN ACTION :D (or wait for incoming Matrix Data)")

        # 2. ENTRADA DEL MENÚ NO BLOQUEANTE (Espera hasta 0.5 segundos por un input)
        # sys.stdin es monitoreado; si no hay entrada, continúa el bucle e inspecciona la red.
        rlist, _, _ = select.select([sys.stdin], [], [], 0.5)

        if rlist:
            opcion = sys.stdin.readline().strip()

            if opcion == "1":
                client.ejecutar_accion("L")
            elif opcion == "2":
                client.ejecutar_accion("O")
                client.cerrar()
                break
            else:
                client.ejecutar_accion("z")

except KeyboardInterrupt:
    print("\n\n[Python] Interrupción detectada. Saliendo de forma segura...")
    if client.esta_corriendo():
        client.ejecutar_accion("O")
        client.cerrar()
