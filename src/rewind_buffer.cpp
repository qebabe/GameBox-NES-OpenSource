#include "rewind_buffer.h"

#include <esp_heap_caps.h>
#include "nes.h"

RewindBuffer::~RewindBuffer() {
    if (records_) heap_caps_free(records_);
    if (recordTypes_) heap_caps_free(recordTypes_);
    if (current_) heap_caps_free(current_);
    if (scratch_) heap_caps_free(scratch_);
}

bool RewindBuffer::begin(uint8_t seconds) {
    if (records_) heap_caps_free(records_);
    if (recordTypes_) heap_caps_free(recordTypes_);
    if (current_) heap_caps_free(current_);
    if (scratch_) heap_caps_free(scratch_);
    records_ = recordTypes_ = current_ = scratch_ = nullptr;
    configuredSeconds_ = seconds;
    if (seconds == 0) {
        stateSize_ = 0;
        capacity_ = 0;
        clear();
        Serial.println("Rewind: disabled");
        return true;
    }
    stateSize_ = nes_.getStateSize();
    capacity_ = (size_t)seconds * 15; // 60 fps / 4-frame sampling
    while (capacity_ >= 15) {
        records_ = (uint8_t*)heap_caps_malloc(stateSize_ * capacity_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        recordTypes_ = (uint8_t*)heap_caps_malloc(capacity_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        current_ = (uint8_t*)heap_caps_malloc(stateSize_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        scratch_ = (uint8_t*)heap_caps_malloc(stateSize_, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (records_ && recordTypes_ && current_ && scratch_) break;
        if (records_) heap_caps_free(records_);
        if (recordTypes_) heap_caps_free(recordTypes_);
        if (current_) heap_caps_free(current_);
        if (scratch_) heap_caps_free(scratch_);
        records_ = recordTypes_ = current_ = scratch_ = nullptr;
        capacity_ -= 15;
    }
    clear();
    if (!records_) {
        Serial.println("Rewind: PSRAM allocation failed; disabled");
        return false;
    }
    Serial.printf("Rewind: %u seconds, %u slots, %u bytes/state\n",
                  actualSeconds(), (unsigned)capacity_, (unsigned)stateSize_);
    return true;
}

void RewindBuffer::clear() {
    head_ = count_ = captureCount_ = 0;
    if (current_ && stateSize_) nes_.saveStateToMemory(current_, stateSize_);
}

uint8_t RewindBuffer::actualSeconds() const {
    return (uint8_t)(capacity_ / 15);
}

bool RewindBuffer::capture() {
    if (!records_ || !current_ || capacity_ == 0) return false;
    uint8_t* slot = records_ + head_ * stateSize_;
    bool fullPrevious = (captureCount_ % 15) == 0;
    if (fullPrevious) memcpy(slot, current_, stateSize_);

    if (!scratch_) return false;
    bool saved = nes_.saveStateToMemory(scratch_, stateSize_);
    if (!saved) return false;
    if (!fullPrevious) {
        for (size_t i = 0; i < stateSize_; i++) slot[i] = current_[i] ^ scratch_[i];
    }
    memcpy(current_, scratch_, stateSize_);
    recordTypes_[head_] = fullPrevious ? 1 : 0;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) count_++;
    captureCount_++;
    return true;
}

bool RewindBuffer::rewindStep() {
    if (!available() || !current_) return false;
    head_ = (head_ + capacity_ - 1) % capacity_;
    uint8_t* record = records_ + head_ * stateSize_;
    if (recordTypes_[head_] == 1) {
        memcpy(current_, record, stateSize_);
    } else {
        for (size_t i = 0; i < stateSize_; i++) current_[i] ^= record[i];
    }
    count_--;
    return nes_.loadStateFromMemory(current_, stateSize_);
}
