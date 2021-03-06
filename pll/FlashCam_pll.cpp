/**********************************************************
 Software developed by Hessel van der Molen
 Main author Hessel van der Molen (hmolen.science at gmail dot com)
 This software is released under BSD license as expressed below
 -------------------------------------------------------------------
 Copyright (c) 2017, Hessel van der Molen
 All rights reserved.
 
 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions
 are met:
 1. Redistributions of source code must retain the above copyright
 notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
 notice, this list of conditions and the following disclaimer in the
 documentation and/or other materials provided with the distribution.
 3. All advertising materials mentioning features or use of this software
 must display the following acknowledgement:
 
 This product includes software developed by Hessel van der Molen
 
 4. None of the names of the author or irs contributors
 may be used to endorse or promote products derived from this software
 without specific prior written permission.
 
 THIS SOFTWARE IS PROVIDED BY Hessel van der Molen ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL AVA BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ****************************************************************/

#include "FlashCam_pll.h"

#include "FlashCam.h"
#include "FlashCam_util_mmal.h"

#include <iostream>
#include <fstream>
#include <string>

#include <stdio.h>
#include <wiringPi.h>
#include <math.h>

#include "interface/mmal/util/mmal_util_params.h"


//PLL settings

// 19.2Mhz is seemingly the basefrequency of the GPIO?
// -> https://www.raspberrypi.org/documentation/hardware/raspberrypi/schematics/Raspberry-Pi-Zero-V1.3-Schematics.pdf
// -> https://pinout.xyz/pinout/gpclk
// -> https://raspberrypi.stackexchange.com/questions/4906/control-hardware-pwm-frequency
// -> TODO: function to determine real frequency
#define RPI_BASE_FREQ 19200000

// WiringPi pin to which PLL is connected
// ( equals GPIO-18 = hardware PWM )
#define PLL_PIN 1

// Accuracy/denominator for fps-update.
#define FPS_DENOMINATOR 256

/* Update-frequency tracker. */
// Maximum error before a measurement is marked as `delayed` 
// ==> measurement > target * max_delay ==> `delayed`
#define FPSREDUCER_MAX_DELAY 1.4
// Number of measurements used to determine if frequency needs to be reduced
#define FPSREDUCER_MEASUREMENTS 15
// Maximum number of measurements which are allowed to be delayed
// NOTE: should be <= FPSREDUCER_MEASUREMENTS and >= 2
#define FPSREDUCER_MAX_DELAYED 12
// state-trackers
//static unsigned int  fpsreducer_idx;
//static unsigned int  fpsreducer_sum;
//static unsigned char fpsreducer_arr[FPSREDUCER_MEASUREMENTS];
//static uint64_t      fpsreducer_prev;



namespace FlashCamPLL {

    //private & static parameterlist
    static FLASHCAM_INTERNAL_STATE_T *_state;

    //Reset GPIO & PWM 
    void resetGPIO();
    //reset PLL parameters
    void clearPLLstate();

    void init(FLASHCAM_INTERNAL_STATE_T *state) {
        
        //when initialised, return.
        if (state->pll_initialised)
            return;
        FlashCamPLL::_state = state;
        
        //clear pll-state
        clearPLLstate();
        FlashCamPLL::_state->pll_active                = false;
        FlashCamPLL::_state->pll_active                = false;
        FlashCamPLL::_state->pll_initialised           = false;
        
        // Check if we have root access.. otherwise system will crash!
        if (getuid()) {
            fprintf(stderr, "%s: FlashCamPLL/WiringPi requires root. Please run with 'sudo'.\n", __func__);
            return;
        //if we are root, init WiringPi
        } else if (wiringPiSetup () == -1) {
            fprintf(stderr, "%s: Cannot init WiringPi.\n", __func__);
            return;
        }
        
        // Set pin-functions
        pinMode( PLL_PIN, PWM_OUTPUT );
        resetGPIO();

        //initialised successfully
        FlashCamPLL::_state->pll_initialised = true;        
    }

    void destroy() {
        resetGPIO();
        FlashCamPLL::_state->pll_initialised = false;        
    }

    void resetGPIO(){
        pwmWrite(PLL_PIN, 0);    
    }

    int update(uint64_t pts, bool *pll_state) {
        //copy pointer to local var: increases readability!
        FLASHCAM_INTERNAL_STATE_T *state = FlashCamPLL::_state;
        *pll_state = false;
        
        if (state->settings->pll_enabled) {
            
    // TIMING UPDATES
            // get frametimings in GPU domain.
            uint64_t frametime_gpu  = pts;
            
            //determine difference
            uint64_t dt_frametime_gpu = 0;
            if (state->pll_last_frametime_gpu != 0)
                dt_frametime_gpu = frametime_gpu - state->pll_last_frametime_gpu;
            //update last measurements
            state->pll_last_frametime_gpu = frametime_gpu;

    #ifndef STEPRESPONSE                
            //correct pll-period toward frames with pll-divider
            float frame_period = state->pll_pwm_period / state->settings->pll_divider;
        
    // FPS VERIFICATIOM
            /*
            // Validate that update rate is close to set frequency. If not, frequency is too high, hence no lock can be obtained.
            // In such a case the target frequency should be adjusted to a more proper value.
            // As an adjustment of the PWM-system may take too much time, it is not updated. 
            // Instead, the PLL mechanism is tricked to think that the PWM sign al runs half the frequency.
            if (state->settings->pll_fpsreducer_enabled) {
                // remove old value from circular buffer / sum
                fpsreducer_sum -= fpsreducer_arr[fpsreducer_idx];
                // set new value
                fpsreducer_arr[fpsreducer_idx] = ((frametime_gpu - fpsreducer_prev) > (pll_fpsperiod * FPSREDUCER_MAX_DELAY)) ? 1 : 0; 
                fpsreducer_sum += fpsreducer_arr[fpsreducer_idx];   
                // check reset
                if ( fpsreducer_sum >= FPSREDUCER_MAX_DELAYED ) {
                    //reduce frequency 
                    // --> factor 2 so we can still sync with PWM
                    state->settings->period  = state->settings->period  * 2;
                    state->pll_framerate = state->pll_framerate / 2;
                    //reset tracker
                    for( int i=0; i<FPSREDUCER_MEASUREMENTS; i++)
                        fpsreducer_arr[i] = 0;
                    fpsreducer_idx  = 0;
                    fpsreducer_sum  = 0;
                    fpsreducer_prev = 0;
                } else {
                    //set new index/value
                    fpsreducer_idx  = (fpsreducer_idx + 1) % FPSREDUCER_MEASUREMENTS;
                    fpsreducer_prev = frametime_gpu;
                }   
                //if (state->settings->verbose)
                //    fprintf(stdout, "PLLtracker: %2d/%2d \n", fpsreducer_sum, FPSREDUCER_MAX_DELAYED);
            }
            */
    // ERROR COMPUTATION

            // Number of pulses since starting PWM signal. 
            // NOTE: Computation assumes that the period of the PWM signal equals the period of the framerate.
            //          This correction is done a couple a lines up. ^^
            uint32_t k = ((frametime_gpu - state->pll_starttime_gpu) / frame_period );

            // Timestamp of last pulse.
            // NOTE: this assumes that the PWM-clock and GPU-clock do not have drift
            uint64_t last_pulsetime_gpu = state->pll_starttime_gpu + (uint64_t)(k * frame_period);
            
            // (Percentual) error with respect to the (corrected) PWM-period.
            // error is with respect to the centre of the estimated interval of the GPU-startime of the hardware PWM
            // NOTE: frametime_gpu > last_pulsetime_gpu.
            int64_t error_us = (frametime_gpu - last_pulsetime_gpu) + state->settings->pll_offset + 0.5*state->pll_startinterval_gpu;
            float error      = error_us / frame_period;
                        
            // if error > 50%
            //  --> captured image is too early: pulse for frame is in the future. 
            //    --> So assume we are correcting for the future-pulse
            if ( error > 0.5 ) {
                error    = error - 1;
                error_us = error * frame_period;
            }
            
            //Determine timeframe of last pulse
            uint32_t state_k = ((frametime_gpu - state->pll_starttime_gpu) / state->pll_pwm_period );
            uint64_t state_last_pulsetime_start_gpu = state->pll_starttime_gpu + (uint64_t)(state_k * state->pll_pwm_period);
            uint64_t state_last_pulsetime_end_gpu   = state_last_pulsetime_start_gpu + (uint64_t) (state->settings->pll_pulsewidth*1000.0);
            if ((state_last_pulsetime_start_gpu <= frametime_gpu) && (frametime_gpu <= state_last_pulsetime_end_gpu)) 
                *pll_state = true;
        
            //update framerate
    #ifdef PLLTUNE
            float P = state->P;
            float I = state->I;
            float D = state->D;
    #else
            //tuning reshults at 30Hz (p=7)
            float P = 0.233 * state->pll_framerate;
            float I = 0.0f;
            float D = 0.0f;
    #endif
            
    // STABILITY COMPUTATION
            
#ifdef PLLTUNE
            unsigned int error_idx_jitter = state->pll_error_idx_jitter;
            unsigned int error_idx_sample = state->pll_error_idx_sample;
            // - error
            state->pll_error_sum                            -= state->pll_error[error_idx_jitter];  // remove earliest error
            state->pll_error[error_idx_jitter]               = error_us;                            // replace
            state->pll_error_sum                            += state->pll_error[error_idx_jitter];  // update sum
            // - avg error
            state->pll_error_avg_sum                        -= state->pll_error_avg[error_idx_sample];
            state->pll_error_avg[error_idx_sample]           = state->pll_error_sum / (float) FLASHCAM_PLL_JITTER;
            state->pll_error_avg_sum                        += state->pll_error_avg[error_idx_sample]; 
            // - avg-derivate
            state->pll_error_avg_dt_sum                     -= state->pll_error_avg_dt[error_idx_sample];
            state->pll_error_avg_dt[error_idx_sample]        = state->pll_error_avg[error_idx_sample] - state->pll_error_avg_last;
            state->pll_error_avg_dt_sum                     += state->pll_error_avg_dt[error_idx_sample]; 
            // - avg-derivate-average
            state->pll_error_avg_dt_avg_sum                 -= state->pll_error_avg_dt_avg[error_idx_sample];
            state->pll_error_avg_dt_avg[error_idx_sample]    = state->pll_error_avg_dt_sum / FLASHCAM_PLL_SAMPLES;
            state->pll_error_avg_dt_avg_sum                 += state->pll_error_avg_dt_avg[error_idx_sample]; 
            // - avg-std
            state->pll_error_avg_std_sum                    -= state->pll_error_avg_std[error_idx_sample];
            state->pll_error_avg_std[error_idx_sample] = 0;
            for (int i=0; i<FLASHCAM_PLL_SAMPLES; i++) {
                float error_avg_diff                         = state->pll_error_avg[i] - state->pll_error_avg[error_idx_sample];
                state->pll_error_avg_std[error_idx_sample]  += error_avg_diff * error_avg_diff;
            }
            state->pll_error_avg_std[error_idx_sample]       = sqrt(state->pll_error_avg_std[error_idx_sample] / (float) FLASHCAM_PLL_SAMPLES);
            state->pll_error_avg_std_sum                    += state->pll_error_avg_std[error_idx_sample] ; 
#endif
            
    // PID UPDATE
            state->pll_integral += ((dt_frametime_gpu/1000.0f) * 0.5 * (error + state->pll_last_error));

            // Compute new rate
            state->pll_pid_framerate  = state->pll_framerate;
            state->pll_pid_framerate += P * error;
            state->pll_pid_framerate += I * state->pll_integral;
            state->pll_pid_framerate += D * (error - state->pll_last_error);
            
            //iteration update
#ifdef PLLTUNE
            state->pll_error_avg_last           = state->pll_error_avg[error_idx_sample] ;
            state->pll_error_avg_dt_last        = state->pll_error_avg_dt[error_idx_sample] ;
            state->pll_error_avg_dt_avg_last    = state->pll_error_avg_dt_avg[error_idx_sample] ;
            state->pll_error_avg_std_last       = state->pll_error_avg_std[error_idx_sample] ;
            state->pll_error_idx_jitter         = (state->pll_error_idx_jitter + 1) % FLASHCAM_PLL_JITTER;
            state->pll_error_idx_sample         = (state->pll_error_idx_sample + 1) % FLASHCAM_PLL_SAMPLES;
#endif

            state->pll_last_error               = error;
            state->pll_last_error_us            = error_us;

            //fprintf(stdout, "%s: PLL error %" PRId64 " \n", __func__, error_us);

    // FPS UPDATE

            //if (state->settings->verbose)
            //    fprintf(stdout, "PLLupdate: %f diff= %6" PRId64 " us (%7.3f %%) / fps=%9.5f Hz (%9.5f Hz) [ %" PRId64 " ]", P, error_us, 100*error, params->framerate, state->pll_framerate, state->settings->pll_startinterval_gpu);
           
            // check if update is within accuracy / MMAL stepsize.
            unsigned int oldf = state->params->framerate * FPS_DENOMINATOR;
            unsigned int newf = state->pll_pid_framerate * FPS_DENOMINATOR;

            if ( oldf == newf)
                return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
            
            // update so that other components use the proper framerate
            state->params->framerate = state->pll_pid_framerate;
            
    #else   /* STEPRESPONSE */
            state->pllframes++;

            //next frequency?
            if ((state->pllframes % state->pllframes_next) == 0) {
                state->pllstep_idx++;
                state->pllstep_idx = state->pllstep_idx % FLASHCAM_PLL_STEPRESPONSE_STEPS;        
            }
            //set framerate
            state->params->framerate = state->pllsteps[state->pllstep_idx];

    #endif  /* STEPRESPONSE */
                    
            //create rationale
            MMAL_RATIONAL_T f;        
            f.den = FPS_DENOMINATOR;
            f.num = (unsigned int) (state->params->framerate * f.den);
            
            //update port.
            MMAL_STATUS_T status;
            MMAL_PARAMETER_FRAME_RATE_T param = {{MMAL_PARAMETER_VIDEO_FRAME_RATE, sizeof(param)}, f};
            if ((status = mmal_port_parameter_set(state->port, &param.hdr)) != MMAL_SUCCESS)
                return FlashCamMMAL::mmal_to_int(status);
        }
        //succes!
        return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
    }

    int start() {
        //copy pointer to local var: increases readability!
        FLASHCAM_INTERNAL_STATE_T *state = FlashCamPLL::_state;

        //initialisation error?
        if (!state->pll_initialised) {
            fprintf(stderr, "%s: FlashCamPLL incorrectly initialised.\n", __func__);
            return 1;
        }
        
        if (state->pll_active) {
            fprintf(stderr, "%s: PLL already running\n", __func__);
            return 1;
        }
        
        if (state->settings->pll_enabled) {
            if (state->settings->verbose)
                fprintf(stdout, "%s: FlashCamPLL starting..\n", __func__);

            // Computations based on:
            // - https://www.raspberrypi.org/forums/viewtopic.php?p=957382#p957382
            // - https://pastebin.com/xTH5jnes
            //
            // From links:
            //    1/f = pwm_range * pwm_clock / RPI_BASE_FREQ
            //
            // SYSTEM PARAMETERS:
            // -----
            // RPI_BASE_FREQ : base frequency of PWM system of RPi. 
            //                 Assumed to be fixed. Might be adjustable by user??
            // pwm_range     : is number of bits within a period.
            // pwm_clock     : divider for RPI_BASE_FREQ to set PWM base frequency.
            //                 Higher basefrequency means more pulses per PWM-Period and hence more accurate signal.
            // pwm_pw        : number of bits within a period (pwm_range) that signal is high.
            //
            // USER DEFINED PARAMETERS:
            // -----
            // frequency     : targeted frequency (Hz) at which a full pwm cycle (high & low) should be produced.
            //                 This frequency is a real-number limited between 0.0 to 120.0 Hz.
            // pulsewidth    : number of milliseconds that the pwm pulse should be high. 
            //                 When 0, a flat line (LOW) is observed. 
            //                 When it equals 1000/frequency, a flat line (HIGH) is observed.
            //                 pulsewidth is clipped to 0 and 1000/frequency.
            //
            // USER PARAMETERS -> SYSTEM PARAMETERS
            // ------
            //
            // According to the datasheets / libraries, maximum values are:
            // pwm_range     : unsigned int, 32 bits (2 - 4294967295)
            // pwm_clock     : unsigned int, 12 bits (2 - 4095)
            // pwm_pw        : unsigned int, 32 bits (0 - 4294967295) 
            //
            // user values:
            // frequency     : float, 0.0 - 120.0
            // pulsewidth    : float, 0.0 - 1000.0/frequency
            //
            // These values are related as following:
            // 
            //    1/frequency = pwm_range * pwm_clock / RPI_BASE_FREQ
            //    dutycycle   = pulsewidth * frequency / 1000
            //    pwm_pw      = dutycycle * pwm_range
            // 
            // Or, assuming pwm_clock and frequency are given values:
            //
            //    (uint)  pwm_range   = RPI_BASE_FREQ / ( pwm_clock * frequency )
            //    (float) dutycycle   = pulsewidth * frequency / 1000
            //    (uint)  pwm_pw      = dutycycle * pwm_range
            //
            // From these computations it can be observed that truncation (float -> int)
            //  occures several steps. Hence, the resulting PWM signal can have a slight
            //  error / inaccuracy when compared to the required settings. 
            // 
            // Some observations:
            // - a large pwm_range will result in a more `true` pwm_pw. 
            // - a low clock and frequency will give a larger pwm_range.
            // - the smallest clock value (pwm_clock) is 2.
            // - the largest range value (pwm_range) is 4294967295.
            // - the maximum frequency is 120Hz.
            //
            // Hence, we can compute the minimal frequency and pulsewidth-resolution.
            //
            // minimal frquency:
            // frequency_min            = 1 / (pwm_range * pwm_clock / RPI_BASE_FREQ)
            // frequency_min            = RPI_BASE_FREQ / ( pwm_range * pwm_clock )
            //                          = 19200000 / ( 4294967295 * 2 )
            //                          ~ 0.00224 Hz.
            //
            // minimal resolution:
            // range_max @120Hz         = RPI_BASE_FREQ / ( pwm_clock * frequency )
            //                          = 19200000 / ( 2 * 120 )
            //                          = 80000
            // resolution @120Hz        = period / pwm_range;
            //                          = 1000 / (frequency * pwm_range);
            //                          = 1000 / (120 * 80000);
            //                          ~ 0.0001042 ms
            //                          ~ 0.1042    us
            //
            // resolution error         = 100 * (resolution / period)
            //                          = 100 * ((period / pwm_range) / period)
            //                          = 100 / pwm_range
            //  - @120Hz                = 0.00125%     
            //  - @0.00224Hz            = 0.0000000234%
            //
            // When pwm_clock is increases with a factor `x`:
            //  - minimal resolution error will increase with `x`.
            //  - minimal frequency will decrease with a factor `x`.
            //
            // Therefore, pwm_clock = 2 is sufficient for PLL.
            
            //reset GPIO
            resetGPIO();
            
            //reset FPS-tracker
            //for( int i=0; i<FPSREDUCER_MEASUREMENTS; i++)
            //    fpsreducer_arr[i] = 0;
            //fpsreducer_idx   = 0;
            //fpsreducer_sum   = 0;
            //fpsreducer_prev  = 0;
            
            //reset PLL-paramaters
            clearPLLstate();
            
            // Setup PWM pin
            pinMode( PLL_PIN, PWM_OUTPUT );
            // We do not want the balanced-pwm mode.
            pwmSetMode( PWM_MODE_MS );   
            
            // Set targeted fps
            float target_frequency  = state->params->framerate / state->settings->pll_divider;
            
            // clock & range
            unsigned int pwm_clock  = 2;
            unsigned int pwm_range  = RPI_BASE_FREQ / ( target_frequency * pwm_clock ); 
            
            // Determine maximum pulse length
            float target_period     = 1000.0f / target_frequency;                   //ms
            
            // Limit pulsewidth to period
            if ( state->settings->pll_pulsewidth > target_period) 
                state->settings->pll_pulsewidth = target_period;
            if ( state->settings->pll_pulsewidth < 0) 
                state->settings->pll_pulsewidth = 0;        
            
            // Map pulsewidth to RPi-range
            float dutycycle         = state->settings->pll_pulsewidth / target_period;
            unsigned int pwm_pw     = dutycycle * pwm_range; 
                    
            // Store PLL/PWM settings
            state->pll_pwm_period  = 1000000.0f / target_frequency;                //us
            state->pll_framerate   = target_frequency * state->settings->pll_divider;     //Hz

            // Show computations?
            if ( state->settings->verbose ) {            
                float real_pw    = ( pwm_pw * target_period) / pwm_range;
                float error_pw   = (state->settings->pll_pulsewidth - real_pw) / state->settings->pll_pulsewidth;
                float resolution = target_period / pwm_range;
                
                fprintf(stdout, "%s: PLL/PWL SETTINGS\n", __func__);
                fprintf(stdout, " - Framerate     : %f\n", state->pll_framerate);
                fprintf(stdout, " - PWM frequency : %f\n", target_frequency);
                fprintf(stdout, " - PWM resolution: %.6f ms\n", resolution );
                fprintf(stdout, " - RPi PWM-clock : %d\n", pwm_clock);
                fprintf(stdout, " - RPi PWM-range : %d\n", pwm_range);
                fprintf(stdout, " - PLL Dutycycle : %.6f %%\n", dutycycle * 100);
                fprintf(stdout, " - PLL Pulsewidth: %.6f ms\n", state->settings->pll_pulsewidth);
                fprintf(stdout, " - PWM Pulsewidth: %d / %d\n", pwm_pw, pwm_range);
                fprintf(stdout, " -     --> in ms : %.6f ms\n", real_pw);
                fprintf(stdout, " - Pulsewidth err: %.6f %%\n", error_pw );
            }
            
            // Set pwm values
            pwmSetRange(pwm_range);
            pwmWrite(PLL_PIN, pwm_pw);

            // Try to get an accurate starttime 
            // --> we are not in a RTOS, so operations might get interrupted. 
            // --> keep setting clock (resetting pwm) untill we get an accurate estimate
            //
            // When investigating the sourcecode of WiringPi, it shows that `pwmSetClock`
            //  already has a buildin-delays of atleast 110us + 1us. 
            // Therefore `max_locktime` should be at least 111us.
            unsigned int max_locktime_us = 300; // --> interval = [0, 189] us.

            //loop trackers..
            unsigned int iter = 0;
            struct timespec t1, t2;
            uint64_t t1_us, t2_us, tgpu_us, tdiff;
            
            do {
                // get start-time
                clock_gettime(CLOCK_MONOTONIC, &t1);
                // set clock (reset PWM)
                pwmSetClock(pwm_clock);
                // get GPU time
                mmal_port_parameter_get_uint64(state->port, MMAL_PARAMETER_SYSTEM_TIME, &tgpu_us);
                // get finished-time
                clock_gettime(CLOCK_MONOTONIC, &t2);
                
                // compute difference..
                t1_us = ((uint64_t) t1.tv_sec) * 1000000 + ((uint64_t) t1.tv_nsec) / 1000;
                t2_us = ((uint64_t) t2.tv_sec) * 1000000 + ((uint64_t) t2.tv_nsec) / 1000;
                tdiff = t2_us - t1_us;
                
                //track iterations
                iter++;
            } while (tdiff > max_locktime_us);
            
            //set startime estimates
            state->pll_starttime_gpu     = tgpu_us - tdiff + 111;
            state->pll_startinterval_gpu = tdiff - 111;
             
            // PLL is activated..
            state->pll_active = true;
            if ( state->settings->verbose ) {
                //clock resolution
                struct timespec tres;
                clock_getres(CLOCK_MONOTONIC, &tres);
                uint64_t res =  ((uint64_t) tres.tv_sec) * 1000000000 + ((uint64_t) tres.tv_nsec);            
                fprintf(stdout, "%s: PLL/PWM start values\n", __func__);
                fprintf(stdout, " - Starttime GPU : %" PRIu64 "us\n", state->pll_starttime_gpu);
                fprintf(stdout, " - Interval GPU  : %" PRIu64 "us\n", state->pll_startinterval_gpu);
                fprintf(stdout, " - Iterations    : %d\n", iter);
                fprintf(stdout, " - Resolution    : %" PRIu64 "ns\n", res);
            }
                
        } else {
            if (state->settings->verbose)
                fprintf(stdout, "%s: FlashCamPLL disabled.\n", __func__);
        }
        
        if ( state->settings->verbose )
            fprintf(stdout, "%s: Succes.\n", __func__);

        return 0;
    }

    int stop() {
        //copy pointer to local var: increases readability!
        FLASHCAM_INTERNAL_STATE_T *state = FlashCamPLL::_state;

        //initialisation error?
        if (!state->pll_initialised) {
            fprintf(stderr, "%s: FlashCamPLL incorrectly initialised.\n", __func__);
            return 1;
        }
        
        if (!state->pll_active) {
            fprintf(stderr, "%s: PLL not running\n", __func__);
            return 0;
        }
        
        if (state->settings->verbose)
            fprintf(stdout, "%s: stopping PLL..\n", __func__);

        //stop PWM
        resetGPIO();
        //reset fps
        state->params->framerate = state->pll_framerate;
        //reset active-flag
        // --> stops PLL-callback from processing new MMAL updates
        state->settings->pll_enabled = false;
        state->pll_active = false;
        
        //wait for callback to process current update
        usleep(1000000); //sleep 1s
        
        if ( state->settings->verbose )
            fprintf(stdout, "%s: Succes.\n", __func__);

        return 0;
    }

    void clearPLLstate() {
        //copy pointer to local var: increases readability!
        FLASHCAM_INTERNAL_STATE_T *state = FlashCamPLL::_state;

        for( int i=0; i<FLASHCAM_PLL_JITTER; i++) {
            state->pll_error[i] = 0;
        }
        for( int i=0; i<FLASHCAM_PLL_SAMPLES; i++) {
            state->pll_error_avg[i]          = 0;
            state->pll_error_avg_dt[i]       = 0;
            state->pll_error_avg_dt_avg[i]   = 0;
            state->pll_error_avg_std[i]      = 0;
        }
        
        state->pll_framerate                 = 0;
        state->pll_pwm_period                = 0;
        state->pll_starttime_gpu             = 0;
        state->pll_startinterval_gpu         = 0;
        state->pll_last_frametime_gpu        = 0;
        state->pll_pid_framerate             = 0;
        state->pll_last_error                = 0;
        state->pll_last_error_us             = 0;
        state->pll_integral                  = 0;
        
        state->pll_error_idx_jitter          = 0;
        state->pll_error_idx_sample          = 0;
        state->pll_error_sum                 = 0;
        state->pll_error_avg_last            = 0;
        state->pll_error_avg_sum             = 0;
        state->pll_error_avg_dt_last         = 0;
        state->pll_error_avg_dt_sum          = 0;
        state->pll_error_avg_dt_avg_last     = 0;
        state->pll_error_avg_dt_avg_sum      = 0;
        state->pll_error_avg_std_last        = 0;
        state->pll_error_avg_std_sum         = 0;
        
        //do not clear
        // float P
        // float I
        // float D
    }

    void getDefaultSettings(FLASHCAM_SETTINGS_T *settings) {
        settings->pll_enabled               = 0;
        settings->pll_divider               = 1;                            // use camera frequency.
        settings->pll_offset                = 0;                            // PWM start == Frame start
        settings->pll_pulsewidth            = 0.5f / VIDEO_FRAME_RATE_NUM;  // 50% duty cycle with default framerate
        settings->pll_fpsreducer_enabled    = 1;                            // Allow PLL to reduce frequency when needed
    }

    void printSettings(FLASHCAM_SETTINGS_T *settings) {
        fprintf(stderr, "PLL Enabled   : %d\n", settings->pll_enabled);
        fprintf(stderr, "PLL Divider   : %d\n", settings->pll_divider);
        fprintf(stderr, "PLL Offset    : %d us\n", settings->pll_offset);
        fprintf(stderr, "PLL Pulsewidth: %0.5f ms\n", settings->pll_pulsewidth);
        fprintf(stderr, "PLL FPSReducer: %d\n", settings->pll_fpsreducer_enabled);
    }

}

/* FLASHCAM FUNCATIONS --> EXTERNAL */

int FlashCam::setPLLEnabled( unsigned int  enabled ) {    
    // Is camera active?
    if (_state.pll_active) {
        fprintf(stderr, "%s: Cannot change PLL-mode while camera is active\n", __func__);
        return FlashCamMMAL::mmal_to_int(MMAL_EINVAL);
    }

    _settings.pll_enabled = enabled;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::getPLLEnabled( unsigned int *enabled ) {
    *enabled = _settings.pll_enabled;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::setPLLPulseWidth( float  pulsewidth ){    
    // Is camera active?
    if (_state.pll_active) {
        fprintf(stderr, "%s: Cannot change PLL-pulsewidth while camera is active\n", __func__);
        return FlashCamMMAL::mmal_to_int(MMAL_EINVAL);
    }

    if (pulsewidth < 0) 
        pulsewidth = 0;
    _settings.pll_pulsewidth = pulsewidth;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::getPLLPulseWidth( float *pulsewidth ) {
    *pulsewidth = _settings.pll_pulsewidth;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::setPLLDivider( unsigned int  divider ){    
    // Is camera active?
    if (_state.pll_active) {
        fprintf(stderr, "%s: Cannot change PLL-divider while camera is active\n", __func__);
        return FlashCamMMAL::mmal_to_int(MMAL_EINVAL);
    }

    if (divider < 1) 
        divider = 1;
    _settings.pll_divider = divider;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::getPLLDivider( unsigned int *divider ) {
    *divider = _settings.pll_divider;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::setPLLOffset( int  offset ){    
    _settings.pll_offset = offset;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::getPLLOffset( int *offset ) {
    *offset = _settings.pll_offset;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::setPLLFPSReducerEnabled( unsigned int  enabled ) {    
    _settings.pll_fpsreducer_enabled = enabled;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}

int FlashCam::getPLLFPSReducerEnabled( unsigned int *enabled ) {
    *enabled = _settings.pll_fpsreducer_enabled;
    return FlashCamMMAL::mmal_to_int(MMAL_SUCCESS);
}
