# Modbus & libmodbus Anleitung (Huawei Wechselrichter)

## 1. Funktionsweise von Modbus (Kurzübersicht)

Modbus ist ein weit verbreitetes, master/slave-basiertes Kommunikationsprotokoll (in der modernen Nomenklatur auch Client/Server genannt). Der **Master (Client)** sendet Anfragen, und der **Slave/Server** (in deinem Fall der Huawei-Wechselrichter) antwortet darauf.

### Modbus RTU vs. Modbus TCP
* **Modbus RTU:** Nutzt serielle Verbindungen (RS485).
* **Modbus TCP:** Nutzt das Ethernet/WLAN-Netzwerk (TCP/IP auf Port `502`).
  * *Huawei Wechselrichter:* Werden üblicherweise über den **Smart Dongle (WLAN-FE)** oder ein integriertes WLAN-Modul per **Modbus TCP** abgefragt.

### Register-Typen
Modbus organisiert Daten in Adressen (Register), auf die lesend oder schreibend zugegriffen werden kann:

| Register-Typ | Zugriff | Datengröße | Beschreibung |
| :--- | :--- | :--- | :--- |
| **Discrete Inputs** | Read-Only | 1 Bit | Digitaler Eingangsstatus |
| **Coils** | Read/Write | 1 Bit | Schaltzustände/Relais |
| **Input Register** | Read-Only | 16 Bit (1 Word) | Analoge Messwerte (z.B. aktuelle Leistung) |
| **Holding Register** | Read/Write | 16 Bit (1 Word) | Einstellungen, Parameter & Messwerte |

> **Hinweis für Huawei:** Huawei-Wechselrichter nutzen vorwiegend **Holding Register** für Messdaten. Da Werte wie Leistung ($W$) oder Energie ($kWh$) oft 32-Bit-Zahlen (Integers oder Float) sind, belegen sie **zwei aufeinanderfolgende 16-Bit-Register**.

---

## 2. Nutzung der `libmodbus` Bibliothek in C++

`libmodbus` ist eine C-Bibliothek, lässt sich aber hervorragend in C++-Projekte einbinden.

### Installation
Auf Debian/Ubuntu-basierten Systemen:
```bash
sudo apt-get install libmodbus-dev
```

### Grundlegender Ablauf im Code
1. **Kontext erstellen:** `modbus_new_tcp(ip, port)`
2. **Slave-ID setzen:** `modbus_set_slave(ctx, slave_id)` (oft Slave-ID `1` oder `0` bei Huawei)
3. **Verbindung herstellen:** `modbus_connect(ctx)`
4. **Register auslesen:** `modbus_read_registers(ctx, addr, count, dest)`
5. **Daten verarbeiten:** Kombinieren von zwei 16-Bit-Registern zu einem 32-Bit-Werten.
6. **Verbindung schließen:** `modbus_close(ctx)` und `modbus_free(ctx)`

---

## 3. C++ Beispielcode

Dieses Beispiel liest typische Werte wie die Eingangsleistung (Register `32064`) oder den Wechselrichter-Status aus.

```cpp
#include <iostream>
#include <cstdint>
#include <modbus/modbus.h>

// Hilfsfunktion: Fügt zwei 16-Bit Register zu einem 32-Bit Integer (Big-Endian) zusammen
uint32_t combine_registers(uint16_t reg_high, uint16_t reg_low) {
    return (static_cast<uint32_t>(reg_high) << 16) | reg_low;
}

int main() {
    const char* IP_ADDRESS = "192.168.1.100"; // IP-Adresse deines Wechselrichters / Smart Dongles
    const int PORT = 502;                      // Standard Modbus TCP Port
    const int SLAVE_ID = 1;                    // Standard-Unit-ID für Huawei

    // 1. Modbus-TCP Kontext erstellen
    modbus_t* ctx = modbus_new_tcp(IP_ADDRESS, PORT);
    if (ctx == nullptr) {
        std::cerr << "Fehler beim Erstellen des Modbus-Kontextes!" << std::endl;
        return -1;
    }

    // Timeout setzen (optional, aber empfohlen)
    modbus_set_response_timeout(ctx, 3, 0); // 3 Sekunden Timeout

    // 2. Slave/Unit ID setzen
    modbus_set_slave(ctx, SLAVE_ID);

    // 3. Verbindung herstellen
    if (modbus_connect(ctx) == -1) {
        std::cerr << "Verbindung zum Wechselrichter fehlgeschlagen: " 
                  << modbus_strerror(errno) << std::endl;
        modbus_free(ctx);
        return -1;
    }

    std::cout << "Erfolgreich mit Wechselrichter verbunden!" << std::endl;

    // 4. Register auslesen
    // Beispiel: PV-Eingangsleistung (Input Power) liegt oft auf Register 32064 (2 Register groß)
    uint16_t raw_registers[2];
    int start_register = 32064;
    int num_registers = 2;

    int rc = modbus_read_registers(ctx, start_register, num_registers, raw_registers);
    
    if (rc == -1) {
        std::cerr << "Fehler beim Lesen der Register: " 
                  << modbus_strerror(errno) << std::endl;
    } else {
        // 5. Daten auswerten
        uint32_t input_power_watts = combine_registers(raw_registers[0], raw_registers[1]);
        std::cout << "Aktuelle PV-Eingangsleistung: " << input_power_watts << " W" << std::endl;
    }

    // 6. Verbindung beenden und Ressourcen freigeben
    modbus_close(ctx);
    modbus_free(ctx);

    return 0;
}
```

### Kompilieren
Um das Programm zu kompilieren, musst du die `libmodbus`-Bibliothek verlinken:

```bash
g++ -o huawei_reader main.cpp $(pkg-config --cflags --libs libmodbus)
```

---

## 4. Wichtige Huawei-Spezifika

1. **Register-Map:** Konsultiere das Dokument *"SUN2000 MODBUS Interface Definitions"* von Huawei für genaue Registeradressen (z. B. `32064` für Eingangsleistung, `32080` für Netzleistung, `37113` für Batterie-Ladezustand/SOC).
2. **Byte-Order / Big-Endian:** Huawei sendet Daten in Big-Endian. Die zwei gelesenen 16-Bit-Werte müssen in der richtigen Reihenfolge zusammengesetzt werden.
3. **Skalierungsfaktoren:** Viele Werte müssen durch `10`, `100` oder `1000` geteilt werden (z. B. Spannung in Volt mit 1 Nachkommastelle).