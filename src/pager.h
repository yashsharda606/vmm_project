#ifndef PAGER_H
#define PAGER_H

#include "types.h"
#include <string>
#include <vector>

class Pager {
public:
    virtual ~Pager() = default;
    virtual frame_t* select_victim_frame() = 0;
    virtual void reset_age(int frame) {}
};

class FIFO_Pager : public Pager {
public:
    FIFO_Pager();
    frame_t* select_victim_frame() override;
private:
    int hand;
};

class Random_Pager : public Pager {
public:
    Random_Pager(const std::string &randfile, int num_frames);
    frame_t* select_victim_frame() override;
private:
    std::vector<int> random_values;
    int ofs;
};

class Clock_Pager : public Pager {
public:
    Clock_Pager();
    frame_t* select_victim_frame() override;
private:
    int hand;
};

class NRU_Pager : public Pager {
public:
    NRU_Pager();
    frame_t* select_victim_frame() override;
private:
    int hand;
    unsigned long long last_reset;
};

class Aging_Pager : public Pager {
public:
    Aging_Pager();
    frame_t* select_victim_frame() override;
    void reset_age(int frame) override;
private:
    int hand;
    unsigned age[MAX_FRAMES]; // Added to store 32-bit age vectors
};

class WorkingSet_Pager : public Pager {
public:
    WorkingSet_Pager();
    frame_t* select_victim_frame() override;
private:
    int hand;
};

#endif