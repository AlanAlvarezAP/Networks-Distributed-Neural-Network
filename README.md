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

| 1 Byte | 4 Bytes | 4 Bytes   | 4 Bytes   | 5 Bytes   | 1 Byte  | 3 Bytes | Variable  | 20 Bytes | Variable | 
|-------------|-------------|-------------|-------------|-------|-------------|---------------|-------------|---------------|-------------|
| Hash (Checksum) | Datagram_id | Total packets   | Order(1,2,..,N)   | Sequence Number of data   | Action | Nickname size | Nickname origin | Payload total size | Payload data | 

> Important to notice is that the datagrams will have a size of 500 bytes where the remaining space in the packet is going to be used for padding with the symbol '#'  
> This protocol is going to be used in all communications but some fields maybe blank or with 0 and also de action will change depending of the type of action

**Actions in the project** 
- L: Login of users
- O: Logout of users
- B: Broadcast the matrix
- M: reaction of the broadcast of the matrix in Clients
- P: reaction of the result of the broadcast in server

## For ACKs
| 1 Byte | 5 Bytes | 3 Bytes | 3 Bytes |
|-------------|-------------|-------------|-------------|
| A | Sequence Number ACK | Total packets segment | Current packet segment |

## For NACKs
| 1 Byte | 5 Bytes | 3 Bytes | 3 Bytes |
|-------------|-------------|-------------|-------------|
| N | Sequence Number NACK | Total packets segment | Current packet segment |

---

### Key Behavior

- When a **timeout occurs**, the value of `TimeoutInterval` is **doubled** to avoid a premature timeout for a subsequent segment that will soon be acknowledged.
- However, as soon as a **segment is received** and `EstimatedRTT` is updated, the `TimeoutInterval` is again computed using the formula above.


