# Digimon World 4 PC

Ein begonnener nativer PC-Port von **Digimon World 4 (USA, GameCube)**.
**Stand 0.1: natives Entwicklungsprogramm und Datenleser; das Spiel ist noch
nicht spielbar.** Es gibt bislang keinen portierten Spielablauf, keine 3D-Szene,
keinen Kampf und keine Spielstände.

Das Windows-x64-Programm verwendet SDL3, OpenGL und Dear ImGui. Es liest deine
lokalen Originaldateien direkt. Die PowerPC-`main.dol` wird zur Analyse gelesen;
Spielcode wird noch nicht ausgeführt. Das Repository enthält keine Spieldateien.

## Was dieser Stand kann

- GameCube-Header und DOL-Sektionen prüfen; die USA-Version identifizieren.
- Beide AFS-Archive ohne vollständiges Laden der großen Dateien indexieren.
- Archiveinträge anzeigen und begrenzte Byte-Vorschauen lesen.
- Alchemy-IGB-v6-Metadaten lesen: Feldtypen, Objekttypen und Vererbung.
- Das originale 96×32-Banner aus `opening.bnr` nativ dekodieren und anzeigen.
- Fensterauflösung, Fenstermodus, VSync und Bildratenlimit der Workbench einstellen.
- MSAA für den OpenGL-Kontext anfordern und die tatsächlich erhaltenen Samples anzeigen.
- Von SDL erkannte Gamepads anzeigen und Eingaben prüfen.
- Einstellungen lokal speichern; fehlerhafte Einstellungen abfangen.

Grafikeinstellungen wirken derzeit auf das **Entwicklungsfenster**. Gespeicherte
Gameplay-, Seitenverhältnis- und Controller-Präferenzen sind Vorbereitung und
implementieren noch keine Spielfunktionen. Insbesondere sind 144 FPS in der
Workbench kein Nachweis für DW4-Gameplay mit 144 FPS.

## Windows bauen

Benötigt: Visual Studio mit **Desktopentwicklung mit C++**, Windows SDK und
CMake/Ninja. Auch Visual-Studio-Insiders-Installationen werden gefunden. Der erste
App-Build lädt zwei per Commit und SHA-256 festgelegte Open-Source-Abhängigkeiten.

```powershell
git clone https://github.com/Dekirai/DW4_PC.git
cd DW4_PC
powershell -ExecutionPolicy Bypass -File scripts/build.ps1 -Package
```

Das Skript baut und testet. Das fertige Paket liegt in `dist/DW4_PC/`.
`Start-DW4-PC.cmd` startet es mit der lokalen Konfiguration in
`local/settings.ini`. Beim ersten Start den entpackten Spielordner unter
**Original data** eintragen und **Remember directory** wählen. Alternativ:

```powershell
.\scripts\run.ps1 -Data 'E:\path\Digimon World 4 - Extracted'
```

Alternativ in einer x64-Developer-Shell:

```powershell
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release -DDW4_BUILD_APP=ON
cmake --build build/Release --parallel
ctest --test-dir build/Release --output-on-failure
cmake --install build/Release --prefix dist/DW4_PC
```

Nur die plattformunabhängigen Datenleser und Tests bauen, ohne Downloads:

```sh
cmake -S . -B build/core -DDW4_BUILD_APP=OFF
cmake --build build/core --config Debug
ctest --test-dir build/core -C Debug --output-on-failure
python -m unittest discover -s tests -p "test_*.py" -v
```

## Mit eigenen Daten starten

Erwartet wird Dolphins entpackte Struktur:

```text
Digimon World 4 - Extracted/
  sys/boot.bin
  sys/main.dol
  sys/fst.bin
  files/area.afs
  files/ndmw.afs
  files/ndmw.igb
  files/opening.bnr
```

```powershell
.\dist\DW4_PC\dw4_pc.exe --data 'E:\path\Digimon World 4 - Extracted'
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted' --json
.\dist\DW4_PC\dw4_inspect.exe 'E:\path\Digimon World 4 - Extracted\files\area.afs' --list
```

Mit Python 3.10+ lässt sich die Extraktion zusätzlich vollständig gegen die
unveränderte ISO prüfen. Das Werkzeug liest ausschließlich und gibt einen
JSON-Bericht aus:

```powershell
python tools/verify_game.py 'E:\path\Digimon World 4 - Extracted' --iso 'E:\path\Digimon World 4 (USA).iso'
```

Referenz: `GDJEB2`, DOL-SHA-1
`e409a88a7379ed1a536f93b0a303a0ce7cd5d877`.
Der geprüfte Disc-Header hat das Revisionsbyte **0**; die Referenz-README nennt
„Rev 1“. Der DOL-Hash stimmt exakt überein. Die Workbench führt strukturelle
Prüfungen aus; das Python-Werkzeug prüft zusätzlich diesen Hash.

## Weiterer Weg

Der nächste Meilenstein ist ein echter nativer Asset-Viewer mit einer
dekodierten Spieltextur und einem Mesh. Anschließend folgt eine originale Szene
mit steuerbarem Digimon. Die rekonstruierte Gameplay- und Engine-Logik ist der
größte noch offene Teil.

[Roadmap und Abnahmekriterien](docs/ROADMAP.md) ·
[Architektur](docs/ARCHITECTURE.md) ·
[Prüfung der vorhandenen Source](docs/UPSTREAM_AUDIT.md) ·
[Validierung dieses Stands](docs/VALIDATION.md)

Die Referenz [ivanno4317/dw4-gc](https://github.com/ivanno4317/dw4-gc) ist eine
laufende GameCube-Decompilation. Sie liefert wertvolle Symbole und
Bibliotheksrekonstruktionen, derzeit aber keinen fertigen Gameplay-Quelltext.
Wir halten die Abhängigkeiten, Herkunft und Grenzen in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) fest.
