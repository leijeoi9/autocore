cmake_minimum_required(VERSION 3.16)
project(AutoCore C)

# ============================================================
# AutoCore — AUTOSAR-inspired in-vehicle gateway software
#
# Layered architecture (top → bottom):
#   app/        Application Layer (SWC)
#   rte/        Runtime Environment (RTE)
#   bsw/        Basic Software Layer
#     ├── sys/      System Services (logger, timer, OS abstraction)
#     ├── can/      CAN Communication Stack
#     │   ├── can_if/   CAN Interface Layer (hardware-independent)
#     │   └── can_tp/   CAN Transport Layer (ISO 15765-2)
#     ├── diag/    Diagnostic Stack (UDS, ISO-TP)
#     ├── tcp_ip/  TCP/IP Services (DoIP, Socket Adapter)
#     ├── mem/     Memory Services (NvM)
#     └── mcal/    Microcontroller Abstraction Layer
# ============================================================

# --- BSW: System Services ---
add_subdirectory(bsw/sys/logger)

# --- BSW: MCAL Drivers ---
add_subdirectory(bsw/mcal/can)

# --- BSW: CAN Interface Layer ---
add_subdirectory(bsw/can/can_if)

# --- BSW: Diagnostic Stack ---
add_subdirectory(bsw/diag/iso_tp)

# --- RTE ---
add_subdirectory(rte)

# --- Application ---
add_subdirectory(app/can_service)
