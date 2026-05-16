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

For the given project, we proposed the next protocol who ensures the **Reliable Data Transfer** 

| ACK(1/0)   | Size of Sequence Number | Sequence Number | Size of Sequence Number ACK | Sequence Number ACK | Order(1,2,3) | Data data | Hash |
|-------|-------------|-------------|-------------|-------------|-------------|---------------|-------------|

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


