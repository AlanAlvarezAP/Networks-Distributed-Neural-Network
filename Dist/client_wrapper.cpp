#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "DataStructure_UDP.hpp"

namespace py = pybind11;

// Instancia global idéntica a client.cpp
Client_Protocols_UDP clp_UDP;

class PyClient {
private:
    int SocketFD;
    struct sockaddr_in stSockAddr;

public:
    PyClient() {
        SocketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(45000);
        inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

        clp_UDP.running = true;
        clp_UDP.logging_status = false;
    }

    void iniciar_hilos() {
        // Hilo de lectura UDP idéntico a read_thread_UDP de client.cpp
        std::thread([this]() {
            char buffer[500];
            sockaddr_in sender;
            socklen_t len = sizeof(sender);
            while (clp_UDP.running) {
                int n = recvfrom(this->SocketFD, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &len);
                if (n <= 0) {
                    continue;
                }
                std::string datagram(buffer, n);
                char action = datagram[14];
                clp_UDP.Cases_Client_UDP(action, datagram, this->SocketFD, sender);
            }
        }).detach();

        // Hilo de timeouts dinámicos idéntico a client.cpp
        std::thread(&Client_Protocols_UDP::TimeoutThread_Client, &clp_UDP, &clp_UDP, this->SocketFD, this->stSockAddr).detach();
    }

    // Ejecuta las acciones ('L' u 'O') de la misma forma que el menú nativo
    void ejecutar_accion(char opcion) {
        // Liberamos GIL ya que la función Login de C++ hace un std::getline interactivo
        py::gil_scoped_release release;
        clp_UDP.Cases_Client_UDP(opcion, std::string{opcion}, this->SocketFD, this->stSockAddr);
    }

    bool esta_corriendo() {
        return clp_UDP.running;
    }

    bool esta_logeado() {
        return clp_UDP.logging_status;
    }

    void cerrar() {
        clp_UDP.running = false;
        close(SocketFD);
    }
};

PYBIND11_MODULE(mi_cliente_udp, m) {
    py::class_<PyClient>(m, "PyClient")
        .def(py::init<>())
        .def("iniciar_hilos", &PyClient::iniciar_hilos)
        .def("ejecutar_accion", &PyClient::ejecutar_accion)
        .def("esta_corriendo", &PyClient::esta_corriendo)
        .def("esta_logeado", &PyClient::esta_logeado)
        .def("cerrar", &PyClient::cerrar);
}
