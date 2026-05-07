#pragma once

typedef struct {
    float kp;
    float ki;
    float kd;
    float output_min;
    float output_max;
    float integral;
    float prev_error;
    float output;
} pid_controller_t;

// Initialize PID controller with given parameters
void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float output_min, float output_max);

// Reset integral and derivative state
void pid_reset(pid_controller_t *pid);

// Compute PID output given setpoint and current measurement
float pid_compute(pid_controller_t *pid, float setpoint, float measurement, float dt);

// Update PID tuning parameters at runtime
void pid_set_tunings(pid_controller_t *pid, float kp, float ki, float kd);
