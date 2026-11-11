/*
 * USB nub for the AIC8800D80 HAL. GPL-2.0.
 * Matches IOUSBHostInterface 368b:8d81 interface 2 (Info.plist personality "aic8800").
 *
 * start() returns at once. A thread opens the pipes and runs the firmware init (ItlAic::bringUp());
 * only then does the wrapper registerService(), which lets AirportItlwm match. Before this, the whole
 * init ran inside AirportItlwm::start() on the matching thread, and with the dongle plugged in at
 * power-on boot sat waiting on it (together with the stuck AicLoader, see AicLoader::didTerminate()).
 */
#include "AicUSBDeviceWrapper.hpp"
#include "ItlAic.hpp"

OSDefineMetaClassAndStructors(AicUSBDeviceWrapper, IOPCIEDeviceWrapper)

// AirportItlwm keeps its workloop and command gate in globals (_fWorkloop/_fCommandGate) and its
// releaseAll() releases and NULLs them. IOPCIEDeviceWrapper::start() overwrote _fWorkloop
// unconditionally, so on a quick replug the new instance's workloop could be released by the old
// instance's teardown. The AIC wrapper waits until the previous one is gone and records the owner.
static AicUSBDeviceWrapper *gAicWorkLoopOwner = nullptr;
static const uint32_t kWorkLoopWaitMs = 30000;
static const uint32_t kCancelWaitMs = 10000;

IOService *AicUSBDeviceWrapper::probe(IOService *provider, SInt32 *score)
{
    IOUSBHostInterface *intf = OSDynamicCast(IOUSBHostInterface, provider);
    if (!intf || IOService::probe(provider, score) != this)
        return NULL;
    if (!fHal) {
        fHal = new ItlAic;
        if (!fHal)
            return NULL;
    }
    fHal->setUSBInterface(intf);
    fHalService = fHal;
    pciNub = NULL;
    return this;
}

bool AicUSBDeviceWrapper::start(IOService *provider)
{
    // Not IOPCIEDeviceWrapper::start(): it creates _fWorkloop and registers immediately.
    if (!IOService::start(provider))
        return false;
    fIntf = OSDynamicCast(IOUSBHostInterface, provider);
    fLock = IOLockAlloc();
    if (!fHal || !fLock || !fIntf || !fIntf->open(this)) {
        IOLog("AIC8800: cannot open USB interface\n");
        fIntf = NULL;
        IOService::stop(provider);
        return false;
    }
    UInt8 builtIn = 0;
    setProperty("built-in", OSData::withBytes(&builtIn, sizeof(builtIn)));
    PMinit();
    registerPowerDriver(this, powerStateArray, 2);
    provider->joinPMtree(this);

    thread_t th;
    fBringUpRunning = true;
    retain();
    if (kernel_thread_start(bringUpThread, this, &th) != KERN_SUCCESS) {
        fBringUpRunning = false;
        release();
        IOLog("AIC8800: cannot start bring-up thread\n");
        PMstop();
        fIntf->close(this);
        fIntf = NULL;
        IOService::stop(provider);
        return false;
    }
    thread_deallocate(th);
    IOLog("AIC8800: USB interface 2 open, bringing up firmware\n");
    return true;
}

void AicUSBDeviceWrapper::bringUpThread(void *arg, wait_result_t)
{
    AicUSBDeviceWrapper *self = static_cast<AicUSBDeviceWrapper *>(arg);
    self->bringUp();
    self->release();
    thread_terminate(current_thread());
}

void AicUSBDeviceWrapper::bringUp()
{
    bool ok = fHal->bringUp();
    if (ok && _fWorkloop) {
        IOLog("AIC8800: waiting for the previous instance to release its workloop\n");
        for (uint32_t waited = 0; _fWorkloop && !fTerminating && waited < kWorkLoopWaitMs; waited += 100)
            IOSleep(100);
    }

    IOLockLock(fLock);
    if (ok && !fTerminating && _fWorkloop) {
        IOLog("AIC8800: previous instance still holds the workloop, giving up (replug the dongle)\n");
        ok = false;
    }
    if (ok && !fTerminating) {
        _fWorkloop = IO80211WorkQueue::workQueue();
        ok = _fWorkloop != NULL;
        if (ok)
            gAicWorkLoopOwner = this;
    }
    ok = ok && !fTerminating;
    fBringUpRunning = false;
    IOLockWakeup(fLock, &fBringUpRunning, false);
    IOLockUnlock(fLock);

    if (ok) {
        IOLog("AIC8800: firmware up, registering\n");
        registerService();                  // AirportItlwm matches and starts from here
    } else {
        IOLog("AIC8800: bring-up %s\n", fTerminating ? "cancelled, device going away" : "failed");
        fHal->shutdownTransport();
        releaseOwnWorkLoop();
    }
}

// Unplug or stop while bring-up may still run: make its USB I/O fail now and wait for the thread.
void AicUSBDeviceWrapper::cancelBringUp()
{
    if (!fLock)
        return;
    IOLockLock(fLock);
    fTerminating = true;
    IOLockUnlock(fLock);
    if (fHal)
        fHal->abortTransport();
    uint64_t deadline;
    clock_interval_to_deadline(kCancelWaitMs, kMillisecondScale, &deadline);
    IOLockLock(fLock);
    while (fBringUpRunning) {
        if (IOLockSleepDeadline(fLock, &fBringUpRunning, deadline, THREAD_UNINT) == THREAD_TIMED_OUT)
            break;
    }
    if (fBringUpRunning)
        IOLog("AIC8800: bring-up thread did not stop in time\n");
    IOLockUnlock(fLock);
}

void AicUSBDeviceWrapper::releaseOwnWorkLoop()
{
    // AirportItlwm::releaseAll() normally released and NULLed it already. If AirportItlwm never
    // started (bring-up raced with unplug), the workloop is still ours.
    if (gAicWorkLoopOwner != this)
        return;
    gAicWorkLoopOwner = nullptr;
    if (_fWorkloop) {
        _fWorkloop->release();
        _fWorkloop = NULL;
    }
}

void AicUSBDeviceWrapper::stop(IOService *provider)
{
    cancelBringUp();
    if (fHal)
        fHal->shutdownTransport();          // no-op when AirportItlwm::stop() already detached the HAL
    releaseOwnWorkLoop();
    IOPCIEDeviceWrapper::stop(provider);
    if (fIntf) {
        fIntf->close(this);
        fIntf = NULL;
    }
}

// Unplug: release the interface so termination of the USB nub can finish.
bool AicUSBDeviceWrapper::didTerminate(IOService *provider, IOOptionBits options, bool *defer)
{
    cancelBringUp();
    if (fIntf) {
        fIntf->close(this);
        fIntf = NULL;
    }
    return IOPCIEDeviceWrapper::didTerminate(provider, options, defer);
}

void AicUSBDeviceWrapper::free()
{
    OSSafeReleaseNULL(fHal);
    fHalService = NULL;
    if (fLock) {
        IOLockFree(fLock);
        fLock = nullptr;
    }
    IOPCIEDeviceWrapper::free();
}
