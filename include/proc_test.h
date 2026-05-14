#ifndef PROC_TEST_H
#define PROC_TEST_H

void pid1_fn (void);
void pid2_fn (void);
void pid3_fn (void);
void pid4_fn (void);
void pid5_fn (void);
void pid6_fn (void);

/* sentinel symbols: placed immediately after each pid_fn by the linker order;
 * use (pid_fn_end - pid_fn) as the size argument to exec_fn */
void pid1_fn_end (void);
void pid2_fn_end (void);
void pid3_fn_end (void);
void pid4_fn_end (void);
void pid5_fn_end (void);
void pid6_fn_end (void);

#endif
