# ai usage - phases 1 and 2

## tool folosit
chatgpt

---

## phase 1

### task 1: parse_condition

#### prompt
am descris structura Report si am cerut o functie care sa parseze un string de forma:
field:operator:value

#### ce a generat ai
- folosirea strchr pentru a gasi delimitatori
- impartirea stringului in 3 parti

#### modificari facute
- am adaugat verificari pentru input invalid
- am asigurat terminarea corecta a stringurilor
- am validat ca fiecare componenta exista

---

### task 2: match_condition

#### prompt
am descris campurile:
- severity (int)
- category (string)
- inspector (string)
- timestamp (time_t)

#### ce a generat ai
- comparatii numerice si pe string
- utilizarea operatorilor ==, !=, <, <=, >, >=

#### modificari facute
- am folosit strtol in loc de atoi
- am separat compararea numerica si cea pe string
- am imbunatatit tratarea operatorilor

---

## phase 2

### task 3: notificare monitor

#### prompt
am cerut o functie care:
- citeste pid din .monitor_pid
- trimite SIGUSR1 folosind kill()

#### ce a generat ai
- citire fisier cu open/read
- conversie pid
- trimitere semnal

#### modificari facute
- am adaugat verificari pentru erori
- am tratat cazul in care fisierul nu exista
- am integrat functia in cmd_add

---

### task 4: remove_district

#### prompt
am cerut implementarea comenzii remove_district folosind:
- fork
- exec pentru rm -rf

#### ce a generat ai
- fork + execlp
- waitpid pentru sincronizare

#### modificari facute
- am adaugat validare nume district
- am sters symlink-ul cu unlink
- am adaugat mesaje de eroare

---

### task 5: monitor_reports

#### prompt
am cerut un program care:
- scrie pid in .monitor_pid
- raspunde la SIGUSR1 si SIGINT

#### ce a generat ai
- folosirea sigaction
- variabile volatile pentru semnale
- bucla cu pause()

#### modificari facute
- am adaugat mesaje clare de output
- am sters fisierul la terminare
- am tratat erori la open si write

---

## probleme in codul generat de ai

- lipsa validari input
- folosirea unor functii nesigure (atoi)
- lipsa tratarii erorilor
- nu era adaptat pentru windows

---

## ce am invatat

- lucrul cu fisiere binare
- apeluri de sistem in c
- procese (fork, exec)
- semnale (sigaction, kill)
- gestionarea permisiunilor
- limitari windows vs linux

---

## concluzie

ai a fost folosit ca suport pentru generare de idei si cod initial, dar implementarea finala a fost verificata, modificata si adaptata cerintelor proiectului.