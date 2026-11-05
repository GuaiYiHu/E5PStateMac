// SPDX-License-Identifier: BSD-2-Clause
#pragma once
#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>

class E5PStateMac final : public IOService {
    OSDeclareDefaultStructors(E5PStateMac)
public:
    bool init(OSDictionary *dictionary=nullptr) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    void free() override;
private:
    IOWorkLoop *loop_;
    IOTimerEventSource *timer_;
    IONotifier *notifier_;
    bool timerAdded_;
    void cleanup();
    void publish();
    static void poll(OSObject *owner,IOTimerEventSource *sender);
    static IOReturn power(void *,void *,UInt32,IOService *,void *,vm_size_t);
};
