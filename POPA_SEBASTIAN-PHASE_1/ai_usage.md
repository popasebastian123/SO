# ai usage - phase 1

## tool folosit
chatgpt

---

## task 1: parse_condition

### prompt folosit
am descris structura Report si am cerut o functie:

int parse_condition(const char *input, char *field, char *op, char *value);

care sa sparga un string de forma:

field:operator:value

exemplu:
severity:>=:2

---

### sugestia ai
ai a sugerat:
- folosirea strchr() pentru a gasi caracterele ':'
- extragerea celor 3 parti
- copierea lor in field, op si value

---

### ce am pastrat
- ideea de a folosi strchr pentru delimitatori
- impartirea stringului in 3 parti

---

### ce am modificat
- am adaugat verificari pentru pointeri null
- am asigurat terminarea corecta a stringurilor
- am verificat ca fiecare parte sa nu fie goala

---

## task 2: match_condition

### prompt folosit
am descris campurile:
- severity (int)
- category (string)
- inspector (string)
- timestamp (time_t)

si am cerut:

int match_condition(Report *r, const char *field, const char *op, const char *value);

---

### sugestia ai
- conversie la int pentru campuri numerice
- folosirea strcmp pentru stringuri
- implementarea operatorilor (==, !=, <, <=, >, >=)

---

### ce am pastrat
- logica de comparare
- tratarea operatorilor

---

### ce am modificat
- am inlocuit atoi cu strtol pentru conversie mai sigura
- am separat compararea numerica de cea pe string
- am facut tratarea operatorilor mai clara si uniforma

---

## probleme in codul generat de ai
- nu valida corect operatorii
- folosea atoi (nesigur)
- amesteca logica in loc sa o separe
- nu trata clar timestamp ca valoare numerica

---

## ce am invatat
- cum sa implementez filtrare flexibila
- importanta verificarii codului generat de ai

---

## concluzie
ai a fost folosit ca suport, dar codul final a fost verificat, corectat si adaptat cerintelor proiectului.