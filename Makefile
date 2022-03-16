all: doctorsoffice

doctorsoffice: DoctorsOffice.c
	gcc -pthread -o DoctorsOffice DoctorsOffice.c

clean:
	rm DoctorsOffice
