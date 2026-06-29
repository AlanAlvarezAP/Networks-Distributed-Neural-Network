#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include "DataStructure_UDP.hpp"

namespace py = pybind11;


Server_Protocols_UDP sv_UDP;

class PyServer {
private:
    int ServerFD;
    struct sockaddr_in stSockAddr;
    bool running;

public:
    PyServer() : running(false), ServerFD(-1) {}

    void iniciar_servidor() {
        ServerFD = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
        memset(&stSockAddr, 0, sizeof(struct sockaddr_in));

        stSockAddr.sin_family = AF_INET;
        stSockAddr.sin_port = htons(45000);
        stSockAddr.sin_addr.s_addr = INADDR_ANY;

        bind(ServerFD, (const struct sockaddr *)&stSockAddr, sizeof(struct sockaddr_in));
        running = true;

        std::thread([this]() {
            char buffer[500];
            sockaddr_in sender;
            socklen_t len = sizeof(sender);

            while (this->running) {
                int n = recvfrom(this->ServerFD, buffer, sizeof(buffer), 0, (sockaddr*)&sender, &len);
                if (n <= 0) {
                    continue;
                }
                std::string datagram(buffer, n);
                char action = datagram[14];

                sv_UDP.Cases_Server(action, datagram, this->ServerFD, sender);
            }
        }).detach();

        std::thread(&Server_Protocols_UDP::TimeoutThread_Server, &sv_UDP, &sv_UDP, ServerFD).detach();
    }

    // Opción 1
    void cargar_matriz_csv() {
        // Liberamos el GIL temporalmente ya que Raw_Matrix_file pedirá std::getline en la terminal
        py::gil_scoped_release release;
        sv_UDP.Raw_Matrix_file(ServerFD);
    }

    // Opción 2
    void mostrar_clientes() {
        print(sv_UDP.client_map);
    }

    int obtener_cantidad_clientes() {
        return sv_UDP.client_map.size();
    }

    void cerrar() {
        if (running) {
            running = false;
            close(ServerFD);
        }
    }
};

PYBIND11_MODULE(mi_servidor_udp, m) {
    py::class_<PyServer>(m, "PyServer")
        .def(py::init<>())
        .def("iniciar_servidor", &PyServer::iniciar_servidor)
        .def("cargar_matriz_csv", &PyServer::cargar_matriz_csv)
        .def("mostrar_clientes", &PyServer::mostrar_clientes)
        .def("obtener_cantidad_clientes", &PyServer::obtener_cantidad_clientes)
        .def("cerrar", &PyServer::cerrar);
}
