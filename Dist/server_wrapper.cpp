#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include "DataStructure_UDP.hpp"

namespace py = pybind11;

// INSTANCIA GLOBAL PARA EL SERVIDOR
Server_Protocols_UDP sv_UDP;

// Clase puente para el Servidor
class PyServer {
private:
    int serverFD;
    struct sockaddr_in stSockAddr;
    bool running;

public:
    PyServer() : running(false) {}

    void iniciar_servidor() {
        serverFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&stSockAddr, 0, sizeof(struct sockaddr_in));
        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(45000);
        stSockAddr.sin_addr.s_addr = INADDR_ANY;

        bind(serverFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in));
        running = true;

        // Hilo de lectura UDP del servidor
        std::thread([this]() {
            char buffer[500];
            sockaddr_in sender;
            socklen_t len = sizeof(sender);
            while(this->running){
                int n = recvfrom(this->serverFD, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &len);
                if(n <= 0) continue;
                std::string datagram(buffer, n);
                char action = datagram[14];
                sv_UDP.Cases_Server(action, datagram, this->serverFD, sender);
            }
        }).detach();

        // Hilo de Timeouts del servidor
        std::thread(&Server_Protocols_UDP::TimeoutThread_Server, &sv_UDP, &sv_UDP, serverFD).detach();
    }

    void cargar_matriz_csv() {
        sv_UDP.Raw_Matrix_file(serverFD);
    }

    void mostrar_clientes() {
        print(sv_UDP.client_map);
    }

    int cantidad_clientes() {
        return sv_UDP.client_map.size();
    }

    void cerrar() {
        this->running = false;
        close(serverFD);
    }
};

PYBIND11_MODULE(mi_servidor_udp, m) {
    py::class_<PyServer>(m, "PyServer")
        .def(py::init<>())
        .def("iniciar_servidor", &PyServer::iniciar_servidor)
        .def("cargar_matriz_csv", &PyServer::cargar_matriz_csv)
        .def("mostrar_clientes", &PyServer::mostrar_clientes)
        .def("cantidad_clientes", &PyServer::cantidad_clientes)
        .def("cerrar", &PyServer::cerrar);
}
