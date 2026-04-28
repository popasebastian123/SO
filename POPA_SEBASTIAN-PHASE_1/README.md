# city_manager - phase 1

## descriere
program in c care simuleaza un sistem de raportare si monitorizare a problemelor de infrastructura urbana.

sistemul permite inspectorilor si managerilor sa gestioneze rapoarte despre probleme din diferite districte (drumuri, iluminat, inundatii etc).

---

## functionalitati implementate

- creare automata de districte (directoare)
- stocare rapoarte in fisier binar (reports.dat)
- listare rapoarte existente
- vizualizare raport individual
- stergere raport (doar manager)
- actualizare prag severitate (doar manager)
- filtrare rapoarte dupa conditii
- logarea tuturor operatiilor in fisier
- creare referinta catre fisierul de rapoarte (simulare symlink pe windows)

---

## structura proiect
POPA_SEBASTIAN-PHASE_1/
├── city_manager.c
├── CMakeLists.txt
├── README.md
├── ai_usage.md
├── downtown/
│ ├── reports.dat
│ ├── district.cfg
│ └── logged_district
├── uptown/
│ ├── reports.dat
│ ├── district.cfg
│ └── logged_district
├── active_reports-downtown
└── active_reports-uptown


---

## descriere fisiere

- reports.dat - fisier binar cu rapoarte
- district.cfg - configurare prag severitate
- logged_district - jurnal operatii
- active_reports-* - referinta catre fisierul reports.dat

---

## compilare

pe linux / wsl: gcc -Wall -Wextra -std=c11 city_manager.c -o city_manager


in clion compilarea se face automat prin cmake

---

## rulare

exemple comenzi:

### adaugare raport
./city_manager --role manager --user alice --add downtown


### listare rapoarte
./city_manager --role inspector --user bob --list downtown


### vizualizare raport
./city_manager --role inspector --user bob --view downtown 1

### filtrare
./city_manager --role inspector --user bob --filter downtown severity:>=:2

### stergere raport
./city_manager --role manager --user alice --remove_report downtown 1

### actualizare prag
./city_manager --role manager --user alice --update_threshold downtown 3


---

## roluri

manager:
- acces complet
- poate sterge rapoarte
- poate modifica configurari

inspector:
- poate adauga rapoarte
- poate vizualiza rapoarte
- nu poate modifica configurari

---

## implementare tehnica

au fost utilizate urmatoarele apeluri de sistem:
- open, read, write, close
- lseek, ftruncate
- stat
- mkdir
- chmod

rapoartele sunt stocate in format binar cu structura fixa.

filtrarea se face prin parcurgerea fisierului si evaluarea conditiilor introduse de utilizator.

---

## utilizare ai

functiile parse_condition si match_condition au fost dezvoltate cu asistenta ai si apoi verificate si adaptate.

detalii complete se gasesc in fisierul ai_usage.md.

---

## observatii

- programul a fost dezvoltat si testat pe windows folosind clion
- din cauza limitarilor windows, link-urile simbolice au fost simulate prin fisiere text
- pe sisteme linux, link-urile simbolice sunt create folosind apelul symlink()

---

## testare

au fost create si testate doua districte:
- downtown
- uptown

fiecare contine rapoarte valide si fisiere generate corect

---

## concluzie

proiectul indeplineste cerintele etapei 1:
- gestionare fisiere si directoare
- utilizare apeluri de sistem
- filtrare date
- gestionare roluri si acces
- organizare persistenta pe disc

nota: proiectul a fost dezvoltat si testat pe windows (clion). din cauza limitarilor platformei, link-urile simbolice au fost simulate prin fisiere text. pe sisteme linux, acestea sunt create folosind symlink().