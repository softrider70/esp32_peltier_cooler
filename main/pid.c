#include "pid.h"

void pid_init(pid_controller_t *pid, float kp, float ki, float kd,
              float output_min, float output_max) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->output_min = output_min;
    pid->output_max = output_max;
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
}

void pid_reset(pid_controller_t *pid) {
    pid->integral = 0.0f;
    pid->prev_error = 0.0f;
    pid->output = 0.0f;
}

float pid_compute(pid_controller_t *pid, float setpoint, float measurement, float dt) {
    float error = setpoint - measurement;

    // Proportional
    float p_term = pid->kp * error;

    // Integral with anti-windup clamping
    pid->integral += error * dt;
    float i_term = pid->ki * pid->integral;

    // Derivative (on measurement to avoid derivative kick)
    float derivative = (error - pid->prev_error) / dt;
    float d_term = pid->kd * derivative;
    pid->prev_error = error;

    // Sum and clamp output
    float output = p_term + i_term + d_term;

    if (output > pid->output_max) {
        output = pid->output_max;
        // Anti-windup: prevent integral from growing further
        pid->integral -= error * dt;
    } else if (output < pid->output_min) {
        output = pid->output_min;
        pid->integral -= error * dt;
    }

    pid->output = output;
    return output;
}

void pid_set_tunings(pid_controller_t *pid, float kp, float ki, float kd) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
}
