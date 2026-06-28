#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "DataStructure_UDP.hpp"

namespace py = pybind11;


Client_Protocols_UDP clp_UDP;


class PyClient {
private:
    int socketFD;
    struct sockaddr_in stSockAddr;

public:
    PyClient() {
        socketFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(45000); // Puerto por defecto
        inet_pton(AF_INET, "127.0.0.1", &stSockAddr.sin_addr);

        clp_UDP.running = true;
        clp_UDP.logging_status = false;
    }

    void iniciar_hilos() {
        // Hilo de lectura UDP del cliente
        std::thread([this]() {
            char buffer[500];
            sockaddr_in sender;
            socklen_t len = sizeof(sender);
            while(clp_UDP.running){
                int n = recvfrom(this->socketFD, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &len);
                if(n <= 0) continue;
                std::string datagram(buffer, n);
                char action = datagram[14];
                clp_UDP.Cases_Client_UDP(action, datagram, this->socketFD, sender);
            }
        }).detach();

        // Hilo de gestión de Timeouts nativo del cliente
        std::thread(&Client_Protocols_UDP::TimeoutThread_Client, &clp_UDP, &clp_UDP, this->socketFD, this->stSockAddr).detach();
    }


    void ejecutar_accion(char opcion) {
        clp_UDP.Cases_Client_UDP(opcion, std::string{opcion}, this->socketFD, this->stSockAddr);
    }

    bool esta_corriendo() { return clp_UDP.running; }
    bool esta_logeado() { return clp_UDP.logging_status; }

    void cerrar() {
        clp_UDP.running = false;
        close(socketFD);
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
