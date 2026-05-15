# Networks-Distributed-Neural-Network
Development of the final project for the CS231 - Networks and Communication course

## ⏱️ Setting and Managing the Retransmission Timeout Interval

Given values of `EstimatedRTT` and `DevRTT`, what value should be used for TCP's timeout interval? Clearly, the interval should be greater than or equal to `EstimatedRTT`, or unnecessary retransmissions would be sent. But the timeout interval should not be too much larger than `EstimatedRTT`; otherwise, when a segment is lost, TCP would not quickly retransmit the segment, leading to large data transfer delays.

It is therefore desirable to set the timeout equal to the `EstimatedRTT` plus some margin. The margin should be large when there is a lot of fluctuation in the `SampleRTT` values; it should be small when there is little fluctuation. The value of `DevRTT` should thus come into play here. All of these considerations are taken into account in TCP's method for determining the retransmission timeout interval:

```math
TimeoutInterval = EstimatedRTT + 4 · DevRTT
```

> An initial `TimeoutInterval` value of **1 second** is recommended [RFC 6298](https://www.rfc-editor.org/rfc/rfc6298).

### 📌 Key Behavior

- When a **timeout occurs**, the value of `TimeoutInterval` is **doubled** to avoid a premature timeout for a subsequent segment that will soon be acknowledged.
- However, as soon as a **segment is received** and `EstimatedRTT` is updated, the `TimeoutInterval` is again computed using the formula above.

---

## 🔄 Jacobson / Karels Algorithm

A new way to calculate the RTT average:

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




