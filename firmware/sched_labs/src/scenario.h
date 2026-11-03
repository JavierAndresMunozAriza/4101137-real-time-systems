/* Every scenario (Lab 06 RM/EDF, Lab 07 inversion/PI) implements this pair. */
#ifndef SCHED_LABS_SCENARIO_H_
#define SCHED_LABS_SCENARIO_H_

const char *scenario_name(void);

/* Run the configured scenario for CONFIG_SCHED_LAB_RUN_S seconds, then print its
 * summary. Returns 0, or a negative errno. */
int scenario_run(void);

#endif /* SCHED_LABS_SCENARIO_H_ */
