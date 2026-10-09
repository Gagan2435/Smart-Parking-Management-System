# Smart Parking Management System

A console-based parking lot simulator written in **C++17**. It handles vehicle entry and exit, zone-based slot allocation, live availability tracking and a rule-driven fee engine, all inside a colour dashboard in the terminal.

No external libraries, no database, no hardware. One source file, one compiler.

> **Scope note:** this is a software simulation of parking-lot logic. It is not embedded firmware and does not read from real sensors.

<img width="667" height="587" alt="Screenshot 2026-10-09 112710" src="https://github.com/user-attachments/assets/3d75b350-6ad4-499f-b1d9-0f05a77a4f87" />


## Features

| Area | What it does |
|---|---|
| **Live dashboard** | Colour slot map (green = free, red = occupied, plate shown on occupied slots), occupancy bar, available/occupied counters, revenue today |
| **Park a vehicle** | Validates the number, blocks duplicates, auto-assigns the first free slot in the right zone, or lets you request a specific slot. Issues a ticket |
| **Remove a vehicle** | Looks up the vehicle, computes the bill, prints a receipt, asks for confirmation, frees the slot |
| **Zones** | Bike (B1-B5), Car (C1-C11), Truck (T1-T4), each with its own hourly rate |
| **Fee engine** | Grace period, hourly rounding, daily cap, loyalty discount (details below) |
| **Slot details** | Pick any slot to see the vehicle, entry time, time parked and the running fee |
| **Search** | Full or partial plate match across parked vehicles and past visits |
| **Reports** | Vehicles served, total and daily revenue, average stay, per-type breakdown, last 5 transactions |
| **Persistence** | Lot state is auto-saved to `parking_state.txt`; every completed visit is appended to `parking_history.csv` (opens in Excel) |
| **Demo mode** | Load sample vehicles and fast-forward a simulated clock to watch fees grow without waiting |

### Edge cases handled

- Parking full, and a single zone full while others still have space
- Duplicate vehicle number
- Invalid or empty vehicle numbers
- Requesting an occupied slot or a slot from the wrong zone
- Removing a vehicle that is not parked
- Non-numeric menu input, and end-of-input (Ctrl+D / Ctrl+Z) without losing data

## Fee rules

| Vehicle | Rate |
|---|---|
| Bike | Rs. 10 / hour |
| Car | Rs. 30 / hour |
| Truck | Rs. 60 / hour |

1. The first **10 minutes are free**.
2. After that, time is **rounded up to the next full hour**.
3. Every 24-hour block is **capped at 8 hours' worth** of fee.
4. A vehicle with **3 or more past visits** gets **10% off**.

All of these values are constants at the top of `main.cpp`, so they are easy to change.

## Getting started

### Requirements

A C++17 compiler (`g++` or `clang++`). On Windows, [MinGW-w64](https://www.mingw-w64.org/) or MSYS2 works well. Windows 10+ terminals are supported (ANSI colours are enabled automatically).

### Run in VS Code

1. Open the project folder in VS Code.
2. Press **Ctrl+Shift+B** to build (uses the included `.vscode/tasks.json`).
3. In the integrated terminal run:
   - Windows: `.\parking.exe`
   - Linux / macOS: `./parking`

### Run from any terminal

```bash
g++ -std=c++17 -O2 main.cpp -o parking
./parking
```

### Quick tour

1. Choose **8 > 1** to load demo vehicles.
2. Choose **1** to park your own vehicle.
3. Choose **8 > 2** and fast-forward a few hours.
4. Choose **2** to remove the vehicle and see the receipt.
5. Choose **7** for the report, then open `parking_history.csv`.

## Project structure

```
smart-parking/
├── main.cpp            # entire application
├── README.md
├── .gitignore
└── .vscode/
    └── tasks.json      # one-key build in VS Code
```

## How it works

- **Data model:** a `vector<Slot>` holds the 20 slots (fixed, with zone derived from slot id); a `vector<Record>` holds completed visits.
- **Slot allocation:** each vehicle type maps to a slot range; the allocator scans that range for the first free slot, which keeps assignment predictable and easy to test.
- **Fee engine:** a single pure function `calcBill(type, seconds, pastVisits)` returns a `Bill`. Keeping it free of I/O made it simple to verify each rule on its own.
- **Time:** all timestamps come from one `nowT()` function. Demo mode just adds an offset to it, which is how the fast-forward feature works without touching the fee logic.
- **Persistence:** plain text and CSV, loaded on start and written after every change.

## Possible next steps

- Reservation / pre-booking of slots
- Multiple floors or lots
- Monthly pass holders
- Replace the text files with SQLite
- Unit tests for `calcBill` and plate validation

## Author

Gagandeep
Computer Engineering student


