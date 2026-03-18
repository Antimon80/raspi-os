# 18.03.2026, Spiegelgasse

## Projektziele

Minimales OS für einen Raspberry Pi 4 mit aufgesetztem Sense HAT:

- Auslesen von Temperatur und Luftfeuchtigkeit
- Mapping auf Farben der LED-Matrix
- Mapping auf Zahlenwerte für Ausgabe auf externes Display bzw. Serial-Terminal (über UART)
- JoyStick:
	- links/rechts: Sensor wechseln
	- oben/unten: Einheiten wechseln, Scrollen durch ein Mini-Menü
	- drücken: öffnet das Mini-Menü (Messung starten/stoppen, Snapshot speichern, Messwerte anzeigen)
	- lang drücken: System reboot/shutdown

## OS-Konzepte

- Memory-Mapped I/O (MMIO): Zugriff auf Hardware-Peripherie
- Treiber: UART-Kommunikation, GPIO-Konfigiration, Sense HAT Komponenten (JoyStick, LED-Matrix)
- Interrupt Handling: Timer Ticks, JoyStick Events
- Task Scheduling: Kernel führt verschiedene Tasks aus, die über einen Timer-Interupt koordiniert werden
- Dynmasiche Speicherverwaltung: Für eine Messwert-History muss dynamisch Speicher alloziert werden

## Zeitplan

- Bootstrapping, UART-Terminal-Ausgabe
- Sense HAT Treiber
- Interrupt Handling für JoyStick
- Sensor-Kalibration
- Mapping auf LED-Matrix
- Heap für Messwert-History
- Menü für Sensorwechsel etc.
- Ausgabe auf Display über HDMI
