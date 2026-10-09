// ============================================================================
//  SMART PARKING MANAGEMENT SYSTEM
//  Console-based simulation in C++17  |  No hardware, no external libraries
//
//  Features : slot allocation by vehicle zone, live colour dashboard,
//             fee engine (grace period, hourly rate, daily cap, loyalty
//             discount), vehicle search, reports, CSV history, auto-save,
//             demo mode with a simulated clock.
// ============================================================================
#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace std;
typedef long long ll;

// ----------------------------- Configuration --------------------------------
const int    TOTAL_SLOTS   = 20;
const int    BIKE_LAST     = 5;    // slots 1-5   : Bike zone  (B1..B5)
const int    CAR_LAST      = 16;   // slots 6-16  : Car zone   (C1..C11)
                                   // slots 17-20 : Truck zone (T1..T4)
const int    GRACE_MINUTES = 10;   // free if the stay is within this window
const int    DAILY_CAP_HRS = 8;    // a 24h block never costs more than 8h
const int    LOYAL_VISITS  = 3;    // 10% off after this many past visits
const double LOYAL_DISCOUNT = 0.10;
const string STATE_FILE    = "parking_state.txt";
const string HISTORY_FILE  = "parking_history.csv";

enum VType { BIKE = 0, CAR = 1, TRUCK = 2 };
const char* TYPE_NAME[] = {"Bike", "Car", "Truck"};
const int   RATE[]      = {10, 30, 60};            // Rs. per hour

// ------------------------------ Data models ---------------------------------
struct Slot   { int id; bool occ; string plate; VType type; time_t entry; };
struct Record { string plate; VType type; int slot; time_t in, out; int fee; };
struct Bill   { int hours; int base; int discount; int total; };

vector<Slot>   slots;
vector<Record> history;
ll clockOffset = 0;   // seconds added to real time (demo mode)

// ------------------------------ ANSI colours --------------------------------
namespace col {
const char* RST  = "\033[0m";
const char* BOLD = "\033[1m";
const char* DIM  = "\033[2m";
const char* RED  = "\033[91m";
const char* GRN  = "\033[92m";
const char* YEL  = "\033[93m";
const char* CYN  = "\033[96m";
const char* MAG  = "\033[95m";
const char* FREE = "\033[42;30m";    // black on green
const char* BUSY = "\033[41;97m";    // white on red
}

void enableANSI() {
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(h, &mode)) SetConsoleMode(h, mode | 0x0004);
#endif
}

// ------------------------------ Small helpers -------------------------------
time_t nowT() { return time(nullptr) + (time_t)clockOffset; }

string fmtTime(time_t t) {
    char buf[32];
    struct tm* tmv = localtime(&t);
    strftime(buf, sizeof buf, "%d-%b %H:%M", tmv);
    return buf;
}
string fmtDur(ll secs) {
    if (secs < 0) secs = 0;
    ll h = secs / 3600, m = (secs % 3600) / 60;
    return to_string(h) + "h " + to_string(m) + "m";
}
bool sameDay(time_t a, time_t b) {
    struct tm x = *localtime(&a);
    struct tm y = *localtime(&b);
    return x.tm_year == y.tm_year && x.tm_yday == y.tm_yday;
}
string trim(const string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}
string upper(string s) { for (auto& c : s) c = (char)toupper((unsigned char)c); return s; }
string center(const string& s, int w) {
    if ((int)s.size() >= w) return s.substr(0, w);
    int l = (w - (int)s.size()) / 2;
    return string(l, ' ') + s + string(w - (int)s.size() - l, ' ');
}
string rep(const string& s, int n) { string r; for (int i = 0; i < n; i++) r += s; return r; }
vector<string> split(const string& s, char d) {
    vector<string> out; string item; stringstream ss(s);
    while (getline(ss, item, d)) out.push_back(item);
    return out;
}

void saveAll();
string readLine(const string& prompt) {
    cout << prompt;
    string s;
    if (!getline(cin, s)) { saveAll(); cout << "\n"; exit(0); }   // Ctrl+D / EOF
    return trim(s);
}
int readInt(const string& prompt, int lo, int hi) {
    while (true) {
        string s = readLine(prompt);
        try {
            size_t pos; int v = stoi(s, &pos);
            if (pos == s.size() && v >= lo && v <= hi) return v;
        } catch (...) {}
        cout << col::RED << "  Please enter a number between " << lo << " and " << hi << "." << col::RST << "\n";
    }
}
void pauseScreen() { readLine(string(col::DIM) + "\n  Press Enter to continue..." + col::RST); }
void clearScreen() { cout << "\033[2J\033[H"; }
void ok(const string& m)   { cout << col::GRN << "  [OK] "  << m << col::RST << "\n"; }
void err(const string& m)  { cout << col::RED << "  [!] "   << m << col::RST << "\n"; }
void info(const string& m) { cout << col::CYN << "  [i] "   << m << col::RST << "\n"; }

// ----------------------------- Slot utilities -------------------------------
VType zoneOf(int id) { return id <= BIKE_LAST ? BIKE : (id <= CAR_LAST ? CAR : TRUCK); }
void zoneRange(VType t, int& lo, int& hi) {
    if (t == BIKE)      { lo = 1;             hi = BIKE_LAST; }
    else if (t == CAR)  { lo = BIKE_LAST + 1; hi = CAR_LAST;  }
    else                { lo = CAR_LAST + 1;  hi = TOTAL_SLOTS; }
}
string slotLabel(int id) {
    VType z = zoneOf(id); int lo, hi; zoneRange(z, lo, hi);
    return string(1, "BCT"[z]) + to_string(id - lo + 1);
}
int occupiedCount() { int n = 0; for (auto& s : slots) n += s.occ; return n; }
int zoneOccupied(VType z) {
    int lo, hi, n = 0; zoneRange(z, lo, hi);
    for (int i = lo; i <= hi; i++) n += slots[i - 1].occ;
    return n;
}
int zoneSize(VType z) { int lo, hi; zoneRange(z, lo, hi); return hi - lo + 1; }
Slot* findByPlate(const string& p) {
    for (auto& s : slots) if (s.occ && s.plate == p) return &s;
    return nullptr;
}
Slot* findByLabel(string l) {
    l = upper(l);
    for (auto& s : slots) if (slotLabel(s.id) == l) return &s;
    return nullptr;
}
Slot* firstFree(VType t) {
    int lo, hi; zoneRange(t, lo, hi);
    for (int i = lo; i <= hi; i++) if (!slots[i - 1].occ) return &slots[i - 1];
    return nullptr;
}
string normalizePlate(const string& raw) {
    string r;
    for (char c : raw) if (c != ' ') r += (char)toupper((unsigned char)c);
    return r;
}
bool validPlate(const string& p) {
    if (p.size() < 4 || p.size() > 12) return false;
    for (char c : p) if (!isalnum((unsigned char)c) && c != '-') return false;
    return true;
}
int visitsOf(const string& plate) {
    int n = 0; for (auto& r : history) n += (r.plate == plate); return n;
}

// ------------------------------- Fee engine ---------------------------------
Bill calcBill(VType t, ll secs, int pastVisits) {
    Bill b{0, 0, 0, 0};
    if (secs <= (ll)GRACE_MINUTES * 60) return b;           // grace period
    int rate = RATE[t];
    int hours = (int)((secs + 3599) / 3600);                // round up
    int cap = rate * DAILY_CAP_HRS;
    int days = hours / 24, rem = hours % 24;
    b.hours = hours;
    b.base  = days * cap + min(rem * rate, cap);
    if (pastVisits >= LOYAL_VISITS) b.discount = (int)lround(b.base * LOYAL_DISCOUNT);
    b.total = b.base - b.discount;
    return b;
}

// ------------------------------ Persistence ---------------------------------
void saveAll() {
    ofstream st(STATE_FILE);
    st << "OFFSET|" << clockOffset << "\n";
    for (auto& s : slots)
        if (s.occ) st << s.id << "|" << s.plate << "|" << (int)s.type << "|" << (ll)s.entry << "\n";
    ofstream hs(HISTORY_FILE);
    hs << "Plate,Type,Slot,EntryEpoch,ExitEpoch,Fee,EntryTime,ExitTime\n";
    for (auto& r : history)
        hs << r.plate << "," << TYPE_NAME[r.type] << "," << slotLabel(r.slot) << ","
           << (ll)r.in << "," << (ll)r.out << "," << r.fee << ","
           << fmtTime(r.in) << "," << fmtTime(r.out) << "\n";
}
void loadAll() {
    slots.clear();
    for (int i = 1; i <= TOTAL_SLOTS; i++) slots.push_back({i, false, "", zoneOf(i), 0});
    ifstream st(STATE_FILE);
    string line;
    while (getline(st, line)) {
        auto p = split(line, '|');
        try {
            if (p.size() == 2 && p[0] == "OFFSET") clockOffset = stoll(p[1]);
            else if (p.size() == 4) {
                int id = stoi(p[0]);
                if (id >= 1 && id <= TOTAL_SLOTS)
                    slots[id - 1] = {id, true, p[1], (VType)stoi(p[2]), (time_t)stoll(p[3])};
            }
        } catch (...) {}
    }
    ifstream hs(HISTORY_FILE);
    getline(hs, line);   // header
    while (getline(hs, line)) {
        auto p = split(line, ',');
        if (p.size() < 6) continue;
        try {
            VType t = p[1] == "Bike" ? BIKE : (p[1] == "Truck" ? TRUCK : CAR);
            int sid = 1;
            Slot* s = findByLabel(p[2]);
            if (s) sid = s->id;
            history.push_back({p[0], t, sid, (time_t)stoll(p[3]), (time_t)stoll(p[4]), stoi(p[5])});
        } catch (...) {}
    }
}

// ------------------------------- UI pieces ----------------------------------
void bar(int used, int total, int width = 24) {
    double pct = total ? (double)used / total : 0;
    int fill = (int)lround(pct * width);
    const char* c = pct < 0.5 ? col::GRN : (pct < 0.8 ? col::YEL : col::RED);
    cout << c << "[" << rep("#", fill) << col::DIM << rep("-", width - fill) << c << "]"
         << col::RST << " " << setw(3) << (int)lround(pct * 100) << "%";
}

void banner(const string& title) {
    clearScreen();
    cout << col::CYN << col::BOLD
         << "  +----------------------------------------------------------+\n"
         << "  |" << center("S M A R T   P A R K I N G   S Y S T E M", 58) << "|\n"
         << "  |" << center(title, 58) << "|\n"
         << "  +----------------------------------------------------------+\n"
         << col::RST;
}

void drawGrid() {
    cout << "\n";
    for (int row = 0; row < 4; row++) {
        for (int line = 0; line < 3; line++) {
            cout << "   ";
            for (int c = 0; c < 5; c++) {
                int idx = row * 5 + c;
                Slot& s = slots[idx];
                string txt = (line == 1) ? (s.occ ? s.plate.substr(0, 9) : slotLabel(s.id))
                           : (line == 2) ? (s.occ ? string(TYPE_NAME[s.type]) : "free") : "";
                cout << (s.occ ? col::BUSY : col::FREE) << center(txt, 11) << col::RST << " ";
            }
            cout << "\n";
        }
        cout << "\n";
    }
    cout << "   " << col::FREE << "  Available  " << col::RST << "   "
         << col::BUSY << "  Occupied  " << col::RST
         << col::DIM << "     Zones: B = Bike, C = Car, T = Truck" << col::RST << "\n";
}

ll todayRevenue() {
    ll r = 0; for (auto& h : history) if (sameDay(h.out, nowT())) r += h.fee; return r;
}

void dashboard() {
    banner("LIVE DASHBOARD");
    int occ = occupiedCount();
    cout << "\n  " << col::BOLD << "Clock : " << col::RST << fmtTime(nowT());
    if (clockOffset) cout << col::MAG << "   (demo clock +" << clockOffset / 3600 << "h)" << col::RST;
    cout << "\n  " << col::BOLD << "Lot   : " << col::RST; bar(occ, TOTAL_SLOTS, 30);
    cout << "\n\n  " << col::GRN << "Available: " << setw(2) << TOTAL_SLOTS - occ << col::RST
         << "    " << col::RED << "Occupied: " << setw(2) << occ << col::RST
         << "    " << col::YEL << "Revenue today: Rs." << todayRevenue() << col::RST << "\n";
    drawGrid();
}

void menu() {
    cout << "\n  " << col::BOLD << "MENU" << col::RST << "\n"
         << "   1. Park a vehicle          5. Search a vehicle\n"
         << "   2. Remove a vehicle        6. Calculate parking fee\n"
         << "   3. View parking status     7. Reports & analytics\n"
         << "   4. Slot details            8. Demo & tools\n"
         << "   0. Save and exit\n\n";
}

// ------------------------------- Features -----------------------------------
void printTicket(const Slot& s) {
    cout << "\n" << col::GRN
         << "  +------------- PARKING TICKET -------------+\n"
         << "  |  Vehicle : " << left << setw(28) << s.plate << "|\n"
         << "  |  Type    : " << setw(28) << TYPE_NAME[s.type] << "|\n"
         << "  |  Slot    : " << setw(28) << slotLabel(s.id) << "|\n"
         << "  |  Entry   : " << setw(28) << fmtTime(s.entry) << "|\n"
         << "  |  Rate    : " << setw(28) << ("Rs." + to_string(RATE[s.type]) + " / hour") << "|\n"
         << "  +------------------------------------------+\n" << right << col::RST;
}

void parkVehicle() {
    banner("PARK A VEHICLE");
    if (occupiedCount() == TOTAL_SLOTS) {
        err("Parking is FULL. No slots available right now.");
        pauseScreen(); return;
    }
    string plate = normalizePlate(readLine("\n  Vehicle number (e.g. PB-10-AB-1234): "));
    if (!validPlate(plate)) {
        err("Invalid number. Use 4-12 letters, digits or '-'.");
        pauseScreen(); return;
    }
    if (Slot* ex = findByPlate(plate)) {
        err("Duplicate entry! " + plate + " is already parked in slot " + slotLabel(ex->id) + ".");
        pauseScreen(); return;
    }
    cout << "\n  Vehicle type:\n";
    for (int t = 0; t < 3; t++)
        cout << "   " << t + 1 << ". " << left << setw(6) << TYPE_NAME[t] << right
             << " Rs." << setw(2) << RATE[t] << "/hr   (free slots: "
             << zoneSize((VType)t) - zoneOccupied((VType)t) << ")\n";
    VType vt = (VType)(readInt("  Choose type: ", 1, 3) - 1);

    if (!firstFree(vt)) {
        err(string("The ") + TYPE_NAME[vt] + " zone is full.");
        for (int t = 0; t < 3; t++)
            if (firstFree((VType)t)) info(string(TYPE_NAME[t]) + " zone still has space (for " + TYPE_NAME[t] + "s only).");
        pauseScreen(); return;
    }
    string pref = readLine("  Preferred slot label (Enter = auto-assign): ");
    Slot* s = nullptr;
    if (pref.empty()) s = firstFree(vt);
    else {
        s = findByLabel(pref);
        if (!s)                    { err("No such slot."); pauseScreen(); return; }
        if (s->type != vt)         { err(string("Slot ") + slotLabel(s->id) + " belongs to the " + TYPE_NAME[s->type] + " zone."); pauseScreen(); return; }
        if (s->occ)                { err("That slot is already occupied."); pauseScreen(); return; }
    }
    s->occ = true; s->plate = plate; s->type = vt; s->entry = nowT();
    saveAll();
    ok("Vehicle parked successfully.");
    printTicket(*s);
    pauseScreen();
}

void printReceipt(const Slot& s, time_t out, const Bill& b, int visits) {
    cout << "\n" << col::YEL
         << "  +--------------- RECEIPT ---------------+\n"
         << "  |  Vehicle  : " << left << setw(25) << s.plate << "|\n"
         << "  |  Slot     : " << setw(25) << (slotLabel(s.id) + " (" + TYPE_NAME[s.type] + ")") << "|\n"
         << "  |  In       : " << setw(25) << fmtTime(s.entry) << "|\n"
         << "  |  Out      : " << setw(25) << fmtTime(out) << "|\n"
         << "  |  Duration : " << setw(25) << fmtDur(out - s.entry) << "|\n"
         << "  |  Billed   : " << setw(25) << (to_string(b.hours) + " hr x Rs." + to_string(RATE[s.type])) << "|\n"
         << "  |  Subtotal : " << setw(25) << ("Rs." + to_string(b.base)) << "|\n";
    if (b.discount)
        cout << "  |  Loyalty  : " << setw(25) << ("-Rs." + to_string(b.discount) + " (" + to_string(visits) + " visits)") << "|\n";
    cout << "  |  TOTAL    : " << setw(25) << ("Rs." + to_string(b.total)) << "|\n"
         << "  +---------------------------------------+\n" << right << col::RST;
    if (b.hours == 0) info("Stay was within the " + to_string(GRACE_MINUTES) + "-minute grace period: no charge.");
}

void removeVehicle() {
    banner("REMOVE A VEHICLE");
    if (occupiedCount() == 0) { info("The lot is empty. Nothing to remove."); pauseScreen(); return; }
    string plate = normalizePlate(readLine("\n  Vehicle number: "));
    Slot* s = findByPlate(plate);
    if (!s) { err("Vehicle '" + plate + "' is not parked here."); pauseScreen(); return; }
    time_t out = nowT();
    int visits = visitsOf(plate);
    Bill b = calcBill(s->type, (ll)(out - s->entry), visits);
    printReceipt(*s, out, b, visits);
    string c = upper(readLine("\n  Confirm payment and exit? (Y/N): "));
    if (c != "Y") { info("Cancelled. Vehicle remains parked."); pauseScreen(); return; }
    history.push_back({s->plate, s->type, s->id, s->entry, out, b.total});
    s->occ = false; s->plate = ""; s->entry = 0;
    saveAll();
    ok("Payment received. Slot is free again. Drive safe!");
    pauseScreen();
}

void viewStatus() {
    dashboard();
    cout << "\n  " << col::BOLD << "Zone occupancy" << col::RST << "\n";
    for (int z = 0; z < 3; z++) {
        cout << "   " << left << setw(6) << TYPE_NAME[z] << right << " ";
        bar(zoneOccupied((VType)z), zoneSize((VType)z), 20);
        cout << "  " << zoneOccupied((VType)z) << "/" << zoneSize((VType)z) << "\n";
    }
    pauseScreen();
}

void slotDetails() {
    dashboard();
    string l = readLine("\n  Enter slot label (e.g. B3, C7, T2): ");
    Slot* s = findByLabel(l);
    if (!s) { err("No such slot."); pauseScreen(); return; }
    cout << "\n  Slot " << col::BOLD << slotLabel(s->id) << col::RST
         << "  |  Zone: " << TYPE_NAME[s->type] << "\n";
    if (!s->occ) ok("This slot is AVAILABLE.");
    else {
        ll secs = (ll)(nowT() - s->entry);
        Bill b = calcBill(s->type, secs, visitsOf(s->plate));
        cout << "  Status   : " << col::RED << "OCCUPIED" << col::RST << "\n"
             << "  Vehicle  : " << s->plate << "\n"
             << "  Entered  : " << fmtTime(s->entry) << "\n"
             << "  Parked   : " << fmtDur(secs) << "\n"
             << "  Fee now  : Rs." << b.total << "\n";
    }
    pauseScreen();
}

void searchVehicle() {
    banner("SEARCH A VEHICLE");
    string q = normalizePlate(readLine("\n  Enter full or partial vehicle number: "));
    if (q.empty()) { err("Nothing entered."); pauseScreen(); return; }
    int found = 0;
    cout << "\n";
    for (auto& s : slots)
        if (s.occ && s.plate.find(q) != string::npos) {
            cout << "  " << col::RED << "PARKED " << col::RST << left << setw(14) << s.plate << right
                 << " slot " << slotLabel(s.id) << ", " << TYPE_NAME[s.type]
                 << ", since " << fmtTime(s.entry) << "\n";
            found++;
        }
    int past = 0;
    for (auto& r : history)
        if (r.plate.find(q) != string::npos && past++ < 5)
            cout << "  " << col::DIM << "PAST   " << left << setw(14) << r.plate << right
                 << " " << fmtTime(r.in) << " -> " << fmtTime(r.out) << ", Rs." << r.fee << col::RST << "\n";
    if (!found && !past) err("No record found for '" + q + "'.");
    pauseScreen();
}

void feeCalculator() {
    banner("PARKING FEE CALCULATOR");
    cout << "\n  Rates: ";
    for (int t = 0; t < 3; t++) cout << TYPE_NAME[t] << " Rs." << RATE[t] << "/hr   ";
    cout << "\n  Rules: first " << GRACE_MINUTES << " min free, hours rounded up, 24h block capped at "
         << DAILY_CAP_HRS << "h, " << (int)(LOYAL_DISCOUNT * 100) << "% off after " << LOYAL_VISITS << " visits.\n\n"
         << "   1. Current bill of a parked vehicle\n"
         << "   2. Estimate for a custom duration\n";
    int ch = readInt("  Choose: ", 1, 2);
    if (ch == 1) {
        string plate = normalizePlate(readLine("  Vehicle number: "));
        Slot* s = findByPlate(plate);
        if (!s) { err("Vehicle not parked."); pauseScreen(); return; }
        ll secs = (ll)(nowT() - s->entry);
        Bill b = calcBill(s->type, secs, visitsOf(plate));
        cout << "\n  Parked for " << fmtDur(secs) << " -> billed " << b.hours << "h"
             << "  =  " << col::YEL << "Rs." << b.total << col::RST << "\n";
    } else {
        int t = readInt("  Type (1=Bike 2=Car 3=Truck): ", 1, 3) - 1;
        int h = readInt("  Hours (0-240): ", 0, 240);
        int m = readInt("  Extra minutes (0-59): ", 0, 59);
        int v = readInt("  Past visits of this vehicle (0-50): ", 0, 50);
        Bill b = calcBill((VType)t, (ll)h * 3600 + m * 60, v);
        cout << "\n  Billed " << b.hours << "h | subtotal Rs." << b.base << " | discount Rs." << b.discount
             << " | " << col::YEL << "TOTAL Rs." << b.total << col::RST << "\n";
    }
    pauseScreen();
}

void reports() {
    banner("REPORTS & ANALYTICS");
    ll total = 0, stay = 0; int cnt[3] = {0, 0, 0}; ll rev[3] = {0, 0, 0};
    for (auto& r : history) { total += r.fee; stay += (ll)(r.out - r.in); cnt[r.type]++; rev[r.type] += r.fee; }
    int n = (int)history.size();
    cout << "\n  Vehicles served : " << n
         << "\n  Total revenue   : Rs." << total
         << "\n  Revenue today   : Rs." << todayRevenue()
         << "\n  Average stay    : " << (n ? fmtDur(stay / n) : string("-"))
         << "\n  Currently parked: " << occupiedCount() << "/" << TOTAL_SLOTS << "\n";
    cout << "\n  " << col::BOLD << "By vehicle type" << col::RST << "\n";
    for (int t = 0; t < 3; t++)
        cout << "   " << left << setw(6) << TYPE_NAME[t] << right << " visits: " << setw(3) << cnt[t]
             << "   revenue: Rs." << rev[t] << "\n";
    cout << "\n  " << col::BOLD << "Last 5 transactions" << col::RST << "\n";
    if (history.empty()) cout << "   (none yet)\n";
    for (int i = max(0, n - 5); i < n; i++) {
        auto& r = history[i];
        cout << "   " << left << setw(14) << r.plate << right << setw(6) << TYPE_NAME[r.type]
             << "  " << fmtTime(r.in) << " -> " << fmtTime(r.out) << "  Rs." << r.fee << "\n";
    }
    cout << col::DIM << "\n  Full history is saved in " << HISTORY_FILE << " (open it in Excel)." << col::RST << "\n";
    pauseScreen();
}

void loadDemo() {
    struct D { const char* p; VType t; int minsAgo; };
    D demo[] = {{"123-ABC", CAR, 95}, {"ASD-POI", CAR, 200}, {"321-CBA", CAR, 30},
                {"123-EGY", CAR, 640}, {"PB11-AB-1", BIKE, 45}, {"TRK-9090", TRUCK, 130}};
    int added = 0;
    for (auto& d : demo) {
        if (findByPlate(d.p)) continue;
        Slot* s = firstFree(d.t);
        if (!s) continue;
        s->occ = true; s->plate = d.p; s->type = d.t; s->entry = nowT() - d.minsAgo * 60;
        added++;
    }
    saveAll();
    ok(to_string(added) + " demo vehicle(s) parked.");
}

void toolsMenu() {
    banner("DEMO & TOOLS");
    cout << "\n   1. Load demo vehicles\n"
         << "   2. Fast-forward the clock (see fees grow)\n"
         << "   3. Reset clock to real time\n"
         << "   4. Clear ALL data (lot + history)\n"
         << "   0. Back\n";
    int ch = readInt("\n  Choose: ", 0, 4);
    if (ch == 1) loadDemo();
    else if (ch == 2) {
        int h = readInt("  Hours to fast-forward (1-72): ", 1, 72);
        clockOffset += (ll)h * 3600; saveAll();
        ok("Clock moved forward by " + to_string(h) + " hour(s).");
    } else if (ch == 3) { clockOffset = 0; saveAll(); ok("Clock reset."); }
    else if (ch == 4) {
        if (readLine("  Type YES to wipe everything: ") == "YES") {
            for (auto& s : slots) { s.occ = false; s.plate = ""; s.entry = 0; }
            history.clear(); clockOffset = 0; saveAll(); ok("All data cleared.");
        } else info("Cancelled.");
    }
    if (ch != 0) pauseScreen();
}

// ---------------------------------- main ------------------------------------
int main() {
    enableANSI();
    loadAll();
    while (true) {
        dashboard();
        menu();
        int ch = readInt("  Choose an option: ", 0, 8);
        switch (ch) {
            case 1: parkVehicle();   break;
            case 2: removeVehicle(); break;
            case 3: viewStatus();    break;
            case 4: slotDetails();   break;
            case 5: searchVehicle(); break;
            case 6: feeCalculator(); break;
            case 7: reports();       break;
            case 8: toolsMenu();     break;
            case 0:
                saveAll();
                clearScreen();
                cout << col::CYN << "\n  Data saved. Thanks for using Smart Parking System!\n\n" << col::RST;
                return 0;
        }
    }
}
