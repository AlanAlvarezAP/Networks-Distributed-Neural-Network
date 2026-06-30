# setup.py
import pybind11
from setuptools import Extension, setup

# Compilador común
common_args = [
    "-std=c++20",
    "-O3",
    "-Wall",
    "-fPIC",
    "-I",
    pybind11.get_include(),
]

# Módulo del servidor
server_module = Extension(
    "mi_servidor_udp",
    sources=["server_wrapper.cpp"],
    language="c++",
    extra_compile_args=common_args,
    extra_link_args=["-pthread"],
)

# Módulo del cliente
client_module = Extension(
    "mi_cliente_udp",
    sources=["client_wrapper.cpp"],
    language="c++",
    extra_compile_args=common_args,
    extra_link_args=["-pthread"],
)

setup(
    name="mi_servidor_udp",
    version="1.0",
    description="Módulo UDP para servidor (C++ con pybind11)",
    ext_modules=[server_module, client_module],
    zip_safe=False,
)
