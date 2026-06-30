# Networks-Distributed-Neural-Network
Development of the final project for the CS231 - Networks and Communication course

## General information
  - **Course**: Networks and Communications
  - **Professor**: Julio Omar Santisteban
  - **Semester**: 2026-1
  - **Group**: CCOMP7-1
  - **Students**:
    - Alan Alvarez Puma
    - Maurizio Luque Soto
    - Sofia Pinto Galvez
    - Sebastian Sanchez Cuno
## Protocol Proposal

For the given project, we proposed the next protocol who ensures the **Reliable Data Transfer** in all data distribución.

## For Normal data

| 1 Byte | 4 Bytes | 4 Bytes   | 5 Bytes   | 1 Byte  | 3 Bytes | Variable  | 20 Bytes | Variable | 
|-------------|-------------|-------------|-------|-------------|---------------|-------------|---------------|-------------|
| Hash (Checksum) | Datagram_id | Total packets  | Sequence Number of data   | Action | Nickname size | Nickname origin | Payload total size | Payload data | 

> Important to notice is that the datagrams will have a size of 500 bytes where the remaining space in the packet is going to be used for padding with the symbol '#'  
> This protocol is going to be used in all communications but some fields maybe blank or with 0 and also de action will change depending of the type of action

**Actions in the project** 
- L: Login of users
- O: Logout of users
- B: Broadcast the matrix
- M: reaction of the broadcast of the matrix in Clients
- P: reaction of the result of the broadcast in server

## ACK Packet Format

| 1 Byte | 4 Bytes | 4 Bytes | 5 Bytes | 1 Byte | 3 Bytes | 20 Bytes |
|--------|---------|---------|---------|--------|---------|----------|
| Checksum | Datagram ID | Total Packets | Sequence Number | A | Nickname Size (0) | Matrix Size (0) |

> **Note:** ACK packets do not include a nickname or matrix content. Therefore, `Nickname Size` and `Matrix Size` are always `0`.

---

## NACK Packet Format

| 1 Byte | 4 Bytes | 4 Bytes | 5 Bytes | 1 Byte | 3 Bytes | 20 Bytes |
|--------|---------|---------|---------|--------|---------|----------|
| Checksum | Datagram ID | Total Packets | Sequence Number | N | Nickname Size (0) | Matrix Size (0) |

> **Note:** NACK packets do not include a nickname or matrix content. Therefore, `Nickname Size` and `Matrix Size` are always `0`.

---
