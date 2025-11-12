# Distributed Computing Labs

This repository contains my completed laboratory works for the **Distributed Computing** course at **ITMO University**, taught by **Michael Kosyakov** (Associate Professor) and **Denis Tarakanov** (Assistant Lecturer).

> All labs were implemented in **C99**, built and tested under **Linux x86_64** environment using the provided framework **libdistributedmodel.so**.

---

## 🧪 Overview

Each laboratory work builds upon the previous one, gradually introducing more complex aspects of distributed systems: from basic inter-process communication to synchronization, distributed banking simulation, Lamport’s logical clocks, and mutual exclusion algorithms.

| Lab        | Title                                          | Key Topics                                                   | Main Implemented Functions                                   |
| ---------- | ---------------------------------------------- | ------------------------------------------------------------ | ------------------------------------------------------------ |
| **Lab #1** | Introduction to Communication Framework        | Basic message passing, process synchronization, STARTED/DONE messages | `parent_work()`, `child_work()`                              |
| **Lab #2** | Distributed Banking System                     | Money transfers, physical clocks, balance history tracking   | `parent_work()`, `child_work()`, `transfer()`                |
| **Lab #3** | Lamport’s Logical Clocks                       | Logical time ordering, pending balances, consistency         | `parent_work()`, `child_work()`, `transfer()` with Lamport clocks |
| **Lab #4** | Distributed Mutual Exclusion (Ricart–Agrawala) | Critical section mutual exclusion, CS_REQUEST/CS_REPLY messages | `parent_work()`, `child_work()`, CS access functions         |



---

## ⚙️ Compilation

Each lab includes its own **Makefile**.
Default target builds the `lab` executable.

```bash
make
```

> ✅ No compilation warnings should be allowed.
> Compiler: `clang >= 3.8` or `gcc >= 5.4`
> Standard: `C99`

---

## ▶️ Execution

The framework library must be preloaded and accessible via `LD_LIBRARY_PATH`.

General format:

```bash
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/path/to/lib/"
LD_PRELOAD=/path/to/lib/libdistributedmodel.so ./lab -l N [arguments]
```

### Examples

- **Lab #1**
  ```bash
  ./lab -l 1 -p 3
  ```

- **Lab #2**
  ```bash
  ./lab -l 2 -p 3 10 20 30
  ```

- **Lab #3**
  ```bash
  ./lab -l 3 -p 3 10 20 30
  ```

- **Lab #4**
  - With mutual exclusion enabled:
    ```bash
    ./lab -l 4 -p 3 -m
    ```
  - Without mutual exclusion:
    ```bash
    ./lab -l 4 -p 3
    ```

---

## 🧠 Key Concepts by Laboratory Work

### **Lab #1 — Introduction to Communication Framework**

- Model of distributed system: parent + multiple child processes connected by FIFO channels
- Communication via `send_multicast()` and `receive()`
- Synchronization based on STARTED/DONE messages
- Parent observes system progress, children log all events

### **Lab #2 — Distributed Banking System**

- Extension of previous model with **balances** and **transfers**
- Introduced message types: `TRANSFER`, `ACK`, `STOP`, `BALANCE_HISTORY`
- Synchronization via physical time (`get_physical_time()`)
- Each child maintains a `BalanceHistory` structure over time
- Parent aggregates all histories and outputs via `print_history()`

### **Lab #3 — Lamport’s Logical Clocks**

- Replaces physical time with **Lamport logical time**
- Each process maintains its own Lamport clock
- Timestamps are attached to every message
- Balances now include **pending money in transfer** (`s_balance_pending_in`)
- Ensures total consistency despite asynchronous communication

### **Lab #4 — Distributed Mutual Exclusion (Ricart–Agrawala Algorithm)**

- Applies Lamport’s clocks to implement distributed **critical section** (CS) access
- Processes exchange `CS_REQUEST` and `CS_REPLY` messages
- Conditional deferral of replies ensures **mutual exclusion**
- Child process calls `print(log_loop_operation_fmt)` inside CS
- Each process enters CS `self_id * 5` times
- Parent provides permission but never enters CS

---

## 🧩 Framework Components

- **libdistributedmodel.so** — binary communication framework
- **labs_headers/**
  - `message.h` – message formats and send/receive API
  - `process.h` – main function prototypes
  - `log.h` – logging utilities and required log formats
  - `banking.h` – banking model data structures and time functions

---

## 💡 Remarks and Environment

- Ubuntu 16.04+ or any modern Linux distribution recommended
- VirtualBox / VMware work fine
- Tested under **clang 14.0** and **gcc 11.0**
- No external dependencies beyond `libdistributedmodel.so`
- All logs are automatically written to **stdout** and **events.log**

