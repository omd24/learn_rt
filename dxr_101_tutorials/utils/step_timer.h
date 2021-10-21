#pragma once

class StepTimer {
private:
    // -- source timing data uses QPC units
    LARGE_INTEGER qpc_frequency_;
    LARGE_INTEGER qpc_last_time_;
    UINT64 qpc_max_delta_;

    // -- derived timing data uses a canonical tick format
    UINT64 elapsed_ticks_;
    UINT64 total_ticks_;
    UINT64 leftover_ticks_;

    // -- data for tracking framerate:
    UINT32 frame_count_;
    UINT32 frames_per_sec_;
    UINT32 frames_this_sec_;
    UINT64 qpc_sec_counter_;

    // -- data for configuring fixed timestep mode
    bool is_fixed_timestep_;
    UINT64 target_elapsed_ticks_;
public:
    static UINT64 const TicksPerSecond = 10'000'000;
    StepTimer () :
        elapsed_ticks_(0),
        total_ticks_(0),
        leftover_ticks_(0),
        frame_count_(0),
        frames_per_sec_(0),
        frames_this_sec_(0),
        qpc_sec_counter_(0),
        is_fixed_timestep_(false),
        target_elapsed_ticks_(TicksPerSecond / 60)
    {
        QueryPerformanceFrequency(&qpc_frequency_);
        QueryPerformanceCounter(&qpc_last_time_);
        // -- init max delta to 1/10 of a sec
        qpc_max_delta_ = qpc_frequency_.QuadPart / 10;
    }

    static double TickToSec (UINT64 ticks) { return static_cast<double>(ticks) / TicksPerSecond; }
    static UINT64 SecToTick (double seconds) { return static_cast<UINT64>(seconds * TicksPerSecond); }

    // -- get elapsed time since prev update call
    UINT64 GetElapsedTicks () const { return elapsed_ticks_; }
    double GetElaspedSeconds () const { return TickToSec(elapsed_ticks_); }
    // -- get total time since start of program
    UINT64 GetTotalTicks () const { return total_ticks_; }
    double GetTotalSeconds () const { return TickToSec(total_ticks_); }
    // -- get total number of updates since start of program
    UINT32 GetFrameCount () const { return frame_count_; }
    // -- get current frame rate
    UINT32 GetFramePerSecond () const { return frames_per_sec_; }
    // -- set whether to use fixed or variable timestep
    void SetFixedTimeStep (bool is_fixed) { is_fixed_timestep_ = is_fixed; }
    // -- set how often call update when in fixed rate mode
    void SetTargetElapsedTicks (UINT64 target_elapsed) { target_elapsed_ticks_ = target_elapsed; }
    void SetTargetElapsedSeconds (double target_elapsed) { target_elapsed_ticks_ = SecToTick(target_elapsed); }
    //
    // -- after an intentional timing discontinuty call this to avoid fixed rate attempting unnecessary catchup
    void ResetElapsedTime () {
        QueryPerformanceCounter(&qpc_last_time_);
        leftover_ticks_  = 0;
        frames_per_sec_ = 0;
        frames_this_sec_ = 0;
        qpc_sec_counter_ = 0;
    }

    typedef void(*LPUDATEFUNC) (void);
    // -- update timer state, calling appropriate LPUDATEFUNC
    void Tick (LPUDATEFUNC update = nullptr) {
        // -- query current time
        LARGE_INTEGER curr_time;
        QueryPerformanceCounter(&curr_time);

        UINT64 dt = curr_time.QuadPart - qpc_last_time_.QuadPart;

        qpc_last_time_ = curr_time;
        qpc_sec_counter_ += dt;

        // -- clamp excessively large delta times (e.g., after a long paused in debugger)
        if (dt > qpc_max_delta_)
            dt = qpc_max_delta_;

        // -- convert QPC units to canonical tick format
        dt *= TicksPerSecond;
        dt /= qpc_frequency_.QuadPart;

        UINT32 last_frame_count = frame_count_;
        if (is_fixed_timestep_) {
            // -- if app is running very close to targt elapsed time (within 1/4 of ms) just clamp the clock
            // -- to exactly math the target value
            if (abs(static_cast<int>(dt - target_elapsed_ticks_)) < TicksPerSecond / 4000)
                dt = target_elapsed_ticks_;

            leftover_ticks_ += dt;
            while (leftover_ticks_ >= target_elapsed_ticks_) {
                elapsed_ticks_ = target_elapsed_ticks_;
                total_ticks_ += target_elapsed_ticks_;
                leftover_ticks_ -= target_elapsed_ticks_;
                ++frame_count_;
                if (update)
                    update();
            }
        } else {
            // -- variable timestep update
            elapsed_ticks_ = dt;
            total_ticks_ += dt;
            leftover_ticks_ = 0;
            ++frame_count_;
            if (update)
                update();
        }
        // -- track frame data
        if (frame_count_ != last_frame_count)
            ++frames_this_sec_;
        if (qpc_sec_counter_ >= static_cast<UINT64>(qpc_frequency_.QuadPart)) {
            frames_per_sec_ = frames_this_sec_;
            frames_this_sec_ = 0;
            qpc_sec_counter_ %= qpc_frequency_.QuadPart;
        }
    }

};

