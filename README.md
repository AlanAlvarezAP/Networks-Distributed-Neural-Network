# Networks-Distributed-Neural-Network
Development of the final project for the CS231 - Networks and Communication course

TimeOut
Book: Computer Networking(Kurose), Page: 241 


<img width="1076" height="758" alt="TimeOut (1)" src="https://github.com/user-attachments/assets/e28bb099-08e7-4fbb-b5ff-77a605c14e66" />


# Algoritmo de Jacobson / Karels

- Nueva forma de calcular el promedio de RTT

- **Diff = sampleRTT - EstRTT**

- **EstRTT = EstRTT + ( δ × Diff )**

- **Dev = Dev + δ ( |Diff| - Dev )**
  
  - donde δ es un factor entre 0 y 1 (Por ejemplo 1/8)

- Considerar varianza cuando fijamos el timeout

- **TimeOut = μ × EstRTT + ϕ × Dev**
  
  - donde μ = 1 y ϕ = 4




