# docOffice
Toy Program to demonstrate my experience with Concurrency, Synchronization, Data Structures,
POSIX and C Programming.

This program is a solution to the famous Doctors Office / Barbershop problem.

## Problem

Consider a hypothetical doctor's office where all the doctors are sleepy. Each doctor
has his own exam room. Each doctor will treat a patient, dismiss them then go to the
waiting room to see if there are more patients waiting. If there are, he brings one to
his exam room and treats them. If there are no more patients hereturns to his exam room
and naps.

Upon the arrival of a patient they will look to see if there is an available doctor (is
one sleeping). If the doctor is sleeping, then patient must wake them up and get treated.
If the doctors are busy then the patient will go to the waiting room and wait if there is
a free waiting room chair. If there is no room for them to wait they will leave.

- Doctors and Patients are represented by individual threads
- Patients will arrive at the office between every 1 to 5 seconds
- Each doctor will treat a patient for 4 to 8 seconds
- Doctors should not treat the same patient,
- Doctors should not be  sleeping when patients need to be seen
- Each doctor shares the work and not just one doctor treats all patients
- Patients are seen on a first come first served basis (FIFO)

## Instructions to Run Program

This program was developed and tested on Ubuntu 20.04 and makes use of standard headers
as well as POSIX library functions. This program should compile fine on other Linux
distributions as well as the BSDs.

1. Run `make`. This will compile the code and generate a binary `DoctorsOffice`.
2. Run `./DoctorsOffice` (`Usage: DoctorsOffice <waitingSize> <patients> <doctors>`)
3. The program will run to completion and print out some stats.
4. Verbose logging is available, logs are written to the syslog. Make
   sure that a logging demon (like `rsyslogd`) is running.
