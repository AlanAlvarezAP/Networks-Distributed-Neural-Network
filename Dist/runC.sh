#!/bin/bash
# Cambia al directorio del script
cd "$(dirname "$0")"

# Abre el servidor (el 'bash -i' mantiene la terminal abierta al terminar)
alacritty -T "Servidor" -e bash -c "./server; bash -i" &

# Pausa de 1 segundo
sleep 1

# Abre los 3 clientes
alacritty -T "Cliente 1" -e bash -c "./client; bash -i" &
alacritty -T "Cliente 2" -e bash -c "./client; bash -i" &
alacritty -T "Cliente 3" -e bash -c "./client; bash -i" &
