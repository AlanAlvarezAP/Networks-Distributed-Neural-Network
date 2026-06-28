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
## Jacobson / Karels Algorithm

The Jacobson/Karels algorithm let us set and manage the retransmission timeout interval using:

```math
Diff = sampleRTT - EstRTT
```

```math
EstRTT = EstRTT + ( δ × Diff )
```

```math
Dev = Dev + δ ( |Diff| - Dev )
```

> where **δ** is a factor between 0 and 1 (for example, **1/8**)

- Takes **variance** into account when setting the timeout:

```math
TimeOut = μ × EstRTT + φ × Dev
```

> where **μ = 1** and **φ = 4**

> An initial `TimeoutInterval` value of **1 second** is recommended [RFC 6298](https://www.rfc-editor.org/rfc/rfc6298).

### Key Behavior

- When a **timeout occurs**, the value of `TimeoutInterval` is **doubled** to avoid a premature timeout for a subsequent segment that will soon be acknowledged.
- However, as soon as a **segment is received** and `EstimatedRTT` is updated, the `TimeoutInterval` is again computed using the formula above.


