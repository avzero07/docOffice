#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define PATIENT_SPAWN_LOWER 1
#define PATIENT_SPAWN_UPPER 5

#define TREATMENT_LOWER 4
#define TREATMENT_UPPER 8

#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <syslog.h>

// Global data structures to be used

/*
 * Struct patient_t represents a patient record. Each patient has
 * their own record.
 * */

// Patient Record (patient_t Struct)

struct patient {
  long pat_id;
  long doc_id;
  bool treated;
  struct patient * next;
  pthread_mutex_t * patient_lock;
  pthread_cond_t * finished_treatment;
};
typedef struct patient patient_t;

// Function to Create and Initialize a Patient Record

patient_t * create_patient_record(long pat_id){
  patient_t * pat;
  pat = malloc(sizeof(patient_t));
  pat->pat_id = pat_id;
  pat->doc_id = -1;
  pat->treated = false;
  pat->next = NULL;
  pat->patient_lock = malloc(sizeof(pthread_mutex_t));
  pthread_mutex_init(pat->patient_lock,NULL);
  pat->finished_treatment = malloc(sizeof(pthread_cond_t));
  pthread_cond_init(pat->finished_treatment,NULL);
  return pat;
}

// Function to Destroy Patient Record

void destroy_patient_record(patient_t * pat){
  if(pat == NULL){
    return;
  }
  pthread_mutex_destroy(pat->patient_lock);
  free(pat->patient_lock);
  pthread_cond_destroy(pat->finished_treatment);
  free(pat->finished_treatment);
  free(pat);
  return;
}

/*
 * A linked list will be used to implement the waiting room queue.
 * Each node in the waiting room will be a pointer to a patient_t struct
 * that represents the record of the patient currently awaiting treatment.
 *
 * The queue will allow for FIFO processing of patients.
 * */

// Waiting Room Queue (wait_room_t linked list)

struct wait_room {
  patient_t * wait_room_head;
  patient_t * wait_room_tail;
  pthread_mutex_t wait_room_lock;
  pthread_cond_t * new_patient;
  pthread_cond_t * treatment; // Perhaps global?
  size_t wait_room_max_capacity;
  size_t wait_room_curr_size;
};
typedef struct wait_room wait_room_t;

/*
 * Function to Queue a Patient (Called by Patient Threads)
 *
 * This function must only be called by a patient thread which
 * currently holds the lock for the waiting room queue.
 * */
bool wait_room_add_patient(wait_room_t * wr, patient_t * pat){
  if(wr->wait_room_curr_size >= wr->wait_room_max_capacity){
    return false;
    // Waiting Room is Full
  }
  else{
    if(wr->wait_room_head == NULL){
      wr->wait_room_head = pat;
      wr->wait_room_tail = pat;
    }
    else{
      wr->wait_room_tail->next = pat;
      wr->wait_room_tail = pat;
    }
    (wr->wait_room_curr_size)++;
  }
  return true;
}

/*
 * De-Queue Patient (Called by Doctor Threads)
 *
 * This function must only be called by a Doctor thread which
 * currently holds the lock for the waiting room queue.
 * */
patient_t * wait_room_remove_patient(wait_room_t * wr){
  if(wr->wait_room_head == NULL){
    return NULL;
  }
  else{
    patient_t * pat = wr->wait_room_head;
    wr->wait_room_head = pat->next;
    (wr->wait_room_curr_size)--;
    if(wr->wait_room_head == NULL){
      wr->wait_room_tail = NULL;
    }
    return pat;
  }
}

/*
 * Global Struct To Store Stats
 * */
struct _stats{
  size_t p_entered;
  size_t p_treated;
  size_t p_not_treated;
};
typedef struct _stats stats_t;

// Init Function for Stats
void init_stats(stats_t * stats){
  stats->p_entered = 0;
  stats->p_treated = 0;
  stats->p_not_treated = 0;
}

/*
 * Define Helper Function Signatures
 * */
void print_stats(stats_t * stats);
int gen_random(int a, int b);

/*============================================
 * Global Variables
 *============================================
 * */

wait_room_t * do_wr;

pthread_mutex_t _wait_room_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t _new_patient = PTHREAD_COND_INITIALIZER;
pthread_cond_t _treatment = PTHREAD_COND_INITIALIZER;

size_t num_patients, num_doctors, num_chairs;

stats_t * st;

/*============================================
 * Thread, Main and Helper Functions Below
 *============================================
 * */

/**
* This function is used to represent treating your patients
*/
void treat_patient(patient_t * pat) {
  /*
   * This function simulates treatment. The only parameter
   * is the record of the patient that the doctor is currently
   * treating.
   *
   * The doctor thread will go to sleep for a random number of
   * seconds between 4 and 8.
   *
   * After treatment, the doctor thread sets the treated flag
   * in the patient record. This is how the patient (which waits
   * on the condvar pat->fininshed_treatment) determines that
   * treatment has completed.
   * */
  syslog(LOG_DEBUG,"Doctor %ld: Treating Patient %ld!\n",pat->doc_id,pat->pat_id);
  sleep(gen_random(TREATMENT_LOWER,TREATMENT_UPPER));
  pat->treated = true;
}


/**
 * This thread is responsible for getting patients from the waiting room
 * to treat and sleep when there are no patients.
 */
void *doctor_thread(void *arg) {

	// Set-up required
	long tid = (long) arg;
  patient_t * curr_pat;

  // Doctor behaviour
  syslog(LOG_DEBUG,"Doctor %ld: Arrived at the Office!\n",tid);
  while(1) {
    // Doctor Enters the Waiting Room
    pthread_mutex_lock(&(do_wr->wait_room_lock));
    syslog(LOG_DEBUG,"Doctor %ld: Checking Waiting Room!\n",tid);
    while(do_wr->wait_room_curr_size == 0){
      /*
       * Doctor behavior when waiting room is empty.
       * */
      if(st->p_entered == num_patients){
        /*
         * Exit Doctor Threads when no more patients are expected to
         * come in (amount specified in command line).
         */
        syslog(LOG_DEBUG,"Doctor %ld: No More patients expected, clocking out!\n",tid);
        pthread_mutex_unlock(&(do_wr->wait_room_lock));
        pthread_exit(NULL);
      }
      /*
       * While more patients are expected and none are currently
       * waiting the doctor takes a nap until woken up by a new patient.
       * */
      syslog(LOG_DEBUG,"Doctor %ld: Empty waiting Room, time for a nap!\n",tid);
      pthread_cond_wait(do_wr->new_patient,&(do_wr->wait_room_lock));
    }
    /*
     * When there are patients waiting in the wait room queue,
     * the doctor will remove the oldest waiting patient (head of
     * the wait_room queue) and treat them.
     * */
    curr_pat = wait_room_remove_patient(do_wr);
    curr_pat->doc_id = tid; // Doctor fills in its ID in the patient record
    (st->p_treated)++; // Increment Count of Patients Treated
    pthread_mutex_lock(curr_pat->patient_lock); // Lock the patient record
    syslog(LOG_DEBUG,"Doctor %ld: Going to Treat Patient %ld!\n",tid,curr_pat->pat_id);
    pthread_cond_signal(do_wr->treatment); // Signal the patient that treatment is starting
    // Unlock the wait_room so that other patients can be processed
    pthread_mutex_unlock(&(do_wr->wait_room_lock));
    // Perform Treatment Here
    treat_patient(curr_pat);
    // Notify patient that treatment is done
    syslog(LOG_DEBUG,"Doctor %ld: Completed Patient %ld's treatment!\n",tid,curr_pat->pat_id);
    pthread_cond_signal(curr_pat->finished_treatment);
    pthread_mutex_unlock(curr_pat->patient_lock); // Unlock patient record
  }
}

/**
 * This thread is responsible for acting as a patient, waking up doctors, waiting for doctors
 * and be treated.
 */
void *patient_thread(void *arg) {

  // Set-up required
  patient_t * my_rec = (patient_t *) arg;
  syslog(LOG_DEBUG,"Patient %ld: Entering Doctors Office!\n",my_rec->pat_id);
  // Patient behaviour
  pthread_mutex_lock(&(do_wr->wait_room_lock)); // Lock the waiting room
  (st->p_entered)++; // Update count of the patients that entered
  /*
   * wait_room_add_queue() attempts to add the patient to the queue in
   * waiting room. It returns True when there is space in the waiting
   * room and the patient is added to the queue. It returns False if
   * there is no space.
   * */
  syslog(LOG_DEBUG,"Patient %ld: Checking Waiting Room!\n",my_rec->pat_id);
  if(wait_room_add_patient(do_wr,my_rec)){
    // Patients waits and signals Doctors
    pthread_cond_signal(do_wr->new_patient); // Attempt to wake any Sleeping Doctors
    // Wait Until a Doctor Calls the Patient for Treatment
    syslog(LOG_DEBUG,"Patient %ld: In the wait room; notified doctors!\n",my_rec->pat_id);
    pthread_cond_wait(do_wr->treatment,&(do_wr->wait_room_lock));
  }
  else{
    /*
     * If there is no space in the waiting room, the patient leaves
     * the doctors office.
     * */
    (st->p_not_treated)++;
    pthread_mutex_unlock(&(do_wr->wait_room_lock));
    syslog(LOG_DEBUG,"Patient %ld: Wait Room is full, leaving office!\n",my_rec->pat_id);
    pthread_exit(NULL);
  }
  /*
   * A doctor has called this patient for treatment, unlock the
   * waiting room so that other patients may be processed.
   * */
  syslog(LOG_DEBUG,"Patient %ld: Doctor %ld will treat me!\n",my_rec->pat_id,my_rec->doc_id);
  pthread_mutex_unlock(&(do_wr->wait_room_lock));

  // Attempt to lock its own patient record
  pthread_mutex_lock(my_rec->patient_lock);
  /*
   * During treatment, the doctor holds the lock for the patient
   * record so that they may make modfications. The patient waits on
   * the conditional variable through which the doctor will signal the
   * end of treatment.
   * */
  while(!(my_rec->treated)){
    pthread_cond_wait(my_rec->finished_treatment,my_rec->patient_lock);
  }
	// Treatment is Over, The Patient Leaves
  syslog(LOG_DEBUG,"Patient %ld: Doctor %ld has treated me, leaving!\n",my_rec->pat_id,my_rec->doc_id);
  pthread_mutex_unlock(my_rec->patient_lock);
  pthread_exit(NULL);
}

int main(int argc, char **argv) {

  /*
   * Set Up Doctors Office
   * */

  // Seed Pseudo Random Generator
  srandom(time(NULL));

  pthread_t * doc_thrds, * pat_thrds;
  patient_t ** pat_records;
  int _num_patients, _num_doctors, _num_chairs;

  /*
   * Retrieve and Validate Command Line Arguments
   * */
	if(argc != 4){
		printf("Usage: DoctorsOffice <waitingSize> <patients> <doctors>\n");
		exit(0);
	}

	// Store commandline options to be used
  _num_chairs = atoi(argv[1]);
  _num_patients = atoi(argv[2]);
  _num_doctors = atoi(argv[3]);

  // Validate commandline options
  if(_num_doctors <= 0){
    fprintf(stderr,"Please enter a non-zero, positive number of Doctors!\n");
    exit(1);
  }
  if(_num_chairs <= 0){
    fprintf(stderr,"Please enter a non-zero, positive number for Waiting Size!\n");
    exit(2);
  }
  if(_num_patients < 0){
    fprintf(stderr,"Please enter a positive number for number of Patients!\n");
    exit(3);
  }
  num_patients = (size_t) _num_patients;
  num_doctors = (size_t) _num_doctors;
  num_chairs = (size_t) _num_chairs;

  /*
   * Allocate and Init Some of the Global Structures
   */

  // Open Logging
  openlog("DoctorsOffice",LOG_NDELAY|LOG_PID|LOG_CONS,LOG_USER);
  syslog(LOG_DEBUG,"Program Commences!\n");

  // Init Wait Room
  do_wr = malloc(sizeof(wait_room_t));
  do_wr->wait_room_head = NULL;
  do_wr->wait_room_tail = NULL;
  do_wr->wait_room_lock = _wait_room_lock;
  do_wr->new_patient = &(_new_patient);
  do_wr->treatment = &(_treatment);
  do_wr->wait_room_max_capacity = num_chairs;
  do_wr->wait_room_curr_size = 0;

  // Init Stats
  st = malloc(sizeof(stats_t));
  init_stats(st);

  // Init Doc Threads
  doc_thrds = malloc(num_doctors * sizeof(pthread_t));

	// Start Doctor Threads
  for(long i=0; i<_num_doctors; i++){
    int rc = pthread_create(&doc_thrds[i],NULL,doctor_thread,(void *) i);
    if(rc){
      fprintf(stderr,"Error Creating Doctor #%ld, RC = %d\n",i,rc);
      fprintf(stderr,"%s\n",strerror(rc));
    }
  }

  // All Doctors Threads Commence by this Point
  syslog(LOG_DEBUG,"Main: Started %zu Doctors;\n",num_doctors);

  // Init Patient Threads and Patient Records
  pat_thrds = malloc(num_patients * sizeof(pthread_t));
  pat_records = malloc(num_patients * sizeof(patient_t *));
	// Start Patient Threads
  for(long i=0; i<_num_patients; i++){
    // New Patients arrive every 1 to 5 seconds
    sleep(gen_random(PATIENT_SPAWN_LOWER,PATIENT_SPAWN_UPPER));
    pat_records[i] = create_patient_record(i);
    int rc = pthread_create(&pat_thrds[i],NULL,patient_thread,(void *) pat_records[i]);
    if(rc){
      fprintf(stderr,"Error Creating Patient #%ld, RC = %d\n",i,rc);
      fprintf(stderr,"%s\n",strerror(rc));
    }
  }

  // All Patient Threads Commence by this Point
  syslog(LOG_DEBUG,"Main: Started %zu Patients;\n",num_patients);

  /*
   * Synchronize Main with Patient and Doctor Threads
   * */

  // Synch with Patient Threads First
  for(long i=0; i<_num_patients; i++){
    int rc = pthread_join(pat_thrds[i],NULL);
    if(rc){
      fprintf(stderr,"Error Waiting for Patient #%ld Termination, RC = %d\n",i,rc);
      fprintf(stderr,"%s\n",strerror(rc));
    }
  }

  syslog(LOG_DEBUG,"All patient threads have exited!\n");

  // Issue a Broadcast to Wake Any Sleeping Doctors
  pthread_cond_broadcast(do_wr->new_patient);

  // Synch with Doctor Threads
  for(long i=0; i<_num_doctors; i++){
    int rc = pthread_join(doc_thrds[i],NULL);
    if(rc){
      fprintf(stderr,"Error Waiting for Doctor #%ld Termination, RC = %d\n",i,rc);
      fprintf(stderr,"%s\n",strerror(rc));
    }
  }

  syslog(LOG_DEBUG,"All doctor threads have exited!\n");

  /*
   * Print Stats
   * */
  print_stats(st);

	// Clean up

  // Destroy Patient Records
  for(long i=0; i<_num_patients; i++){
    destroy_patient_record(pat_records[i]);
  }
  syslog(LOG_DEBUG,"All patient records have been destroyed!\n");
  // Destroy Globally Declared Mutex and Condvars
  pthread_cond_destroy(do_wr->treatment);
  pthread_cond_destroy(do_wr->new_patient);
  pthread_mutex_destroy(&(do_wr->wait_room_lock));
  syslog(LOG_DEBUG,"All synch objects have been destroyed!\n");
  // Free Remaining Memory
  free(pat_records);
  free(pat_thrds);
  free(doc_thrds);
  free(st);
  free(do_wr);
  syslog(LOG_DEBUG,"All dynamic memory has been freed!\n");
  // Close Logging
  syslog(LOG_DEBUG,"Shutting Down DoctorsOffice!\n");
  closelog();
	return 0;
}

/*
 * Helper Functions
 * */

// Function to Dump Stats to stdout
void print_stats(stats_t * stats){
  printf("=========================\n");
  printf("Doctors Office Statistics\n");
  printf("=========================\n");
  printf("Number of Chairs  : %zu\n",num_chairs);
  printf("Number of Patients: %zu\n",num_patients);
  printf("Number of Doctors : %zu\n",num_doctors);
  printf("================================================\n");
  printf("Number of Patients that visited             : %zu\n",stats->p_entered);
  printf("Number of Patients Treated                  : %zu\n",stats->p_treated);
  printf("Number of Patients Left without treatment   : %zu\n",stats->p_not_treated);
  printf("================================================\n");
}

// Function to Generate Random Integers
int gen_random(int a, int b){
  int res;
  if(a >= b){
      fprintf(stderr,"Invalid Range! Flipping arguments!\n");
      int temp = a;
      a = b;
      b = temp;
    }
  res = (rand() % (b - a + 1))+a;
  return res;
}
