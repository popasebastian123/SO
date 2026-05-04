# city_manager - phase 1 + phase 2

## descriere
program in c care simuleaza un sistem de raportare si monitorizare a problemelor de infrastructura urbana.

aplicatia permite inspectorilor si managerilor sa gestioneze rapoarte despre probleme din diferite districte (drumuri, iluminat, inundatii etc) si sa monitorizeze activitatea printr-un proces separat.

---

## functionalitati

### phase 1
- creare si organizare districte (directoare separate)
- stocare rapoarte in fisier binar (reports.dat)
- listare rapoarte
- vizualizare raport individual
- filtrare rapoarte dupa conditii
- stergere raport (manager only)
- actualizare prag severitate (manager only)
- logarea operatiilor in fisier
- creare referinte catre fisierele de rapoarte (simulare symlink pe windows)

### phase 2
- comanda remove_district pentru stergerea completa a unui district
- creare proces copil folosind fork
- executie comanda externa rm -rf prin exec
- program separat monitor_reports
- comunicare intre procese folosind semnale
- notificare monitor la adaugare raport (SIGUSR1)
- tratarea semnalelor folosind sigaction
- gestionare fisier .monitor_pid

---

## structura proiect

POPA_SEBASTIAN-PHASE_2/
├── city_manager.c
├── monitor_reports.c
├── CMakeLists.txt
├── README.md
├── AI_usage-phases_1_and_2.md
├── downtown/
│   ├── reports.dat
│   ├── district.cfg
│   └── logged_district
├── uptown/
│   ├── reports.dat
│   ├── district.cfg
│   └── logged_district
├── active_reports-downtown
└── active_reports-uptown

---

## descriere fisiere

- reports.dat - fisier binar care contine rapoarte
- district.cfg - fisier text cu pragul de severitate
- logged_district - jurnal operatii
- active_reports-* - referinta catre reports.dat (simulare symlink pe windows)
- .monitor_pid - fisier temporar creat de monitor

---

## compilare

pe linux / wsl:

gcc city_manager.c -o city_manager
gcc monitor_reports.c -o monitor_reports

in clion compilarea se face automat prin cmake

---

## rulare

### pornire monitor
./monitor_reports

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

### stergere district
./city_manager --role manager --user alice --remove_district downtown

### actualizare prag
./city_manager --role manager --user alice --update_threshold downtown 3

---

## roluri

manager:
- acces complet
- poate sterge rapoarte
- poate sterge districte
- poate modifica configurari

inspector:
- poate adauga rapoarte
- poate vizualiza rapoarte
- nu poate modifica configurari

---

## monitor

programul monitor_reports:
- creeaza fisier .monitor_pid la pornire
- salveaza pid-ul procesului
- asteapta semnale
- la SIGUSR1 afiseaza mesaj (raport nou)
- la SIGINT afiseaza mesaj si se inchide
- la inchidere sterge fisierul .monitor_pid

---

## comunicare intre procese

- city_manager citeste pid din .monitor_pid
- trimite semnal SIGUSR1 folosind kill()
- monitorul receptioneaza semnalul si afiseaza mesaj
- in caz de eroare, logul mentioneaza esecul notificarii

---

## observatii

- proiectul a fost dezvoltat pe windows folosind clion
- unele functionalitati (semnale, fork, exec, symlink) functioneaza complet doar pe linux sau wsl
- pe windows, notificarea monitorului este simulata si va esua controlat

---

## testare

- au fost create minim 2 districte (downtown, uptown)
- au fost adaugate minim 5 rapoarte
- toate comenzile au fost testate
- log-urile sunt generate corect
- remove_district functioneaza

---

## cerinte indeplinite

- utilizare apeluri de sistem: open, read, write, close, unlink
- utilizare procese: fork, exec
- utilizare semnale: sigaction, kill
- gestionare fisiere si directoare
- comunicare intre procese
- organizare persistenta a datelor

---

## concluzie

proiectul implementeaza toate cerintele pentru phase 1 si phase 2:
- gestiune fisiere si directoare
- filtrare si organizare date
- procese si executie comenzi externe
- comunicare prin semnale
- monitorizare evenimente
