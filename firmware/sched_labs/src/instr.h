/* Instrumentation pins shared by the scheduling-lab scenarios (see the overlay). */
#ifndef SCHED_LABS_INSTR_H_
#define SCHED_LABS_INSTR_H_

enum lab_pin {
	LAB_PIN_T1,
	LAB_PIN_T2,
	LAB_PIN_T3,
	LAB_PIN_AUX,
	LAB_PIN_COUNT,
};

/* Configure every pin as an inactive output. Returns 0 or a negative errno. */
int instr_init(void);

/* Drive one pin. A failed edge is not actionable, so there is no return value. */
void instr_set(enum lab_pin pin, int level);

#endif /* SCHED_LABS_INSTR_H_ */
