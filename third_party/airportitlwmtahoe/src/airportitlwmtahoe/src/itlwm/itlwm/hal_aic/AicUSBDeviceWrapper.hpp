/*
 * USB nub for the AIC8800D80 HAL. GPL-2.0.
 *
 * Subclasses IOPCIEDeviceWrapper so the existing "NetworkController" personality
 * (IOProviderClass IOPCIEDeviceWrapper) and AirportItlwm::probe() accept it; pciNub stays NULL.
 */
#ifndef AicUSBDeviceWrapper_hpp
#define AicUSBDeviceWrapper_hpp

#include "../../AirportItlwm/IOPCIEDeviceWrapper.hpp"
#include <IOKit/usb/IOUSBHostInterface.h>
#include <kern/thread.h>

class ItlAic;

class AicUSBDeviceWrapper : public IOPCIEDeviceWrapper {
    OSDeclareDefaultStructors(AicUSBDeviceWrapper)

public:
    IOService *probe(IOService *provider, SInt32 *score) override;
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    bool didTerminate(IOService *provider, IOOptionBits options, bool *defer) override;
    void free() override;

private:
    static void bringUpThread(void *arg, wait_result_t);
    void bringUp();
    void cancelBringUp();
    void releaseOwnWorkLoop();

    IOUSBHostInterface *fIntf = nullptr;
    ItlAic *fHal = nullptr;                 // the wrapper's own reference; AirportItlwm::probe() takes another
    IOLock *fLock = nullptr;
    bool fTerminating = false;              // under fLock
    bool fBringUpRunning = false;           // under fLock
};

#endif /* AicUSBDeviceWrapper_hpp */
