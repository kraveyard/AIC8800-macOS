/*
 * AIC8800D80 cold-boot bring-up. GPL-2.0.
 *
 * The dongle enumerates in three personalities (AIC8800-macOS/docs/PROTOCOL.md):
 *   a69c:5721  fake CD-ROM (mass storage)  -> SCSI START STOP UNIT (eject) switches it to the bootloader
 *   a69c:8d80  ROM bootloader              -> upload firmware (replay of Linux aic_load_fw, AICLoaderSeq.h)
 *   368b:8d81  firmware running            -> AicUSBDeviceWrapper / ItlAic take over
 * AicLoader matches the first two; each step makes the device re-enumerate, which ends this instance.
 */
#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostDevice.h>
#include <IOKit/usb/IOUSBHostPipe.h>
#include <kern/thread.h>
#include <libkern/OSAtomic.h>
#include "AICLoaderSeq.h"

#define LDLOG(fmt, ...) IOLog("AIC8800 loader: " fmt "\n", ##__VA_ARGS__)

class AicLoader : public IOService {
    OSDeclareDefaultStructors(AicLoader)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;
    bool didTerminate(IOService *provider, IOOptionBits options, bool *defer) override;

private:
    IOUSBHostInterface *fIntf = nullptr;
    IOUSBHostPipe *fOut = nullptr, *fIn = nullptr;
    IOBufferMemoryDescriptor *fTx = nullptr, *fRx = nullptr;
    bool fMassStorage = false;
    volatile SInt32 fStop = 0, fRunning = 0;

    static void threadEntry(void *arg, wait_result_t);
    void eject();
    void upload();
    bool write(const void *data, uint32_t len);
    void releaseAll();
    void shutdown();
};

OSDefineMetaClassAndStructors(AicLoader, IOService)

static const uint32_t kBuf = 4096;

bool AicLoader::start(IOService *provider)
{
    if (!IOService::start(provider))
        return false;
    fIntf = OSDynamicCast(IOUSBHostInterface, provider);
    if (!fIntf || !fIntf->open(this)) {
        LDLOG("cannot open interface");
        fIntf = nullptr;
        return false;
    }
    fIntf->retain();
    fMassStorage = fIntf->getDevice()->getDeviceDescriptor()->idProduct == 0x5721;
    // mass storage: bulk 0x02 OUT / 0x82 IN; bootloader: bulk 0x01 OUT / 0x82 IN
    fOut = fIntf->copyPipe(fMassStorage ? 0x02 : 0x01);
    fIn = fIntf->copyPipe(0x82);
    fTx = fIntf->createIOBuffer(kIODirectionOut, kBuf);
    fRx = fIntf->createIOBuffer(kIODirectionIn, kBuf);
    thread_t th;
    OSIncrementAtomic(&fRunning);
    retain();
    if (!fOut || !fIn || !fTx || !fRx || kernel_thread_start(threadEntry, this, &th) != KERN_SUCCESS) {
        LDLOG("pipe/buffer/thread setup failed");
        OSDecrementAtomic(&fRunning);
        release();
        releaseAll();
        return false;
    }
    thread_deallocate(th);
    LDLOG("%s mode, starting", fMassStorage ? "mass-storage" : "bootloader");
    return true;
}

void AicLoader::shutdown()
{
    OSIncrementAtomic(&fStop);
    if (fIn)
        fIn->abort(IOUSBHostIOSource::kAbortSynchronous, kIOReturnAborted, nullptr);
    if (fOut)
        fOut->abort(IOUSBHostIOSource::kAbortSynchronous, kIOReturnAborted, nullptr);
    for (int i = 0; i < 300 && fRunning; i++)
        IOSleep(10);
    if (fRunning) {                                  // thread still inside io(): freeing its pipes would be a UAF
        LDLOG("loader thread did not exit, keeping pipes");
        return;
    }
    releaseAll();
}

// Every step of the loader ends with the device dropping off the bus. IOKit only calls stop() once all
// clients have closed the terminated provider, so closing the interface in stop() alone deadlocks: the
// loader stays "inactive, busy 1" forever, the old Aic MSC / AIC Wlan devices are never freed, and the
// registry never goes quiet ("busy timeout ... 'AicLoader'", and boot waiting on it with the dongle in).
bool AicLoader::didTerminate(IOService *provider, IOOptionBits options, bool *defer)
{
    shutdown();
    return IOService::didTerminate(provider, options, defer);
}

void AicLoader::stop(IOService *provider)
{
    shutdown();
    IOService::stop(provider);
}

void AicLoader::releaseAll()
{
    OSSafeReleaseNULL(fOut);
    OSSafeReleaseNULL(fIn);
    OSSafeReleaseNULL(fTx);
    OSSafeReleaseNULL(fRx);
    if (fIntf) {
        fIntf->close(this);
        OSSafeReleaseNULL(fIntf);
    }
}

void AicLoader::threadEntry(void *arg, wait_result_t)
{
    AicLoader *self = (AicLoader *)arg;
    if (self->fMassStorage)
        self->eject();
    else
        self->upload();
    OSDecrementAtomic(&self->fRunning);
    self->release();
    thread_terminate(current_thread());
}

bool AicLoader::write(const void *data, uint32_t len)
{
    if (len > kBuf)
        return false;
    memcpy(fTx->getBytesNoCopy(), data, len);
    uint32_t done = 0;
    return fOut->io(fTx, len, done, 2000) == kIOReturnSuccess && done == len;
}

// Like `eject /dev/sdX` (Linux udev rule): ALLOW MEDIUM REMOVAL, then START STOP UNIT with LoEj. Linux needed
// several rounds before the chip dropped off the bus, so repeat until the device goes away.
void AicLoader::eject()
{
    static const uint8_t cdbs[2][6] = {{0x1e, 0, 0, 0, 0, 0}, {0x1b, 0, 0, 0, 0x02, 0}};
    for (int round = 0; round < 30 && !fStop; round++) {
        for (int c = 0; c < 2 && !fStop; c++) {
            uint8_t cbw[31] = {'U', 'S', 'B', 'C'};
            cbw[4] = (uint8_t)(round * 2 + c + 1);   // tag
            cbw[14] = 6;                             // CB length; data length 0, flags 0, LUN 0
            memcpy(cbw + 15, cdbs[c], 6);
            if (!write(cbw, sizeof(cbw)))
                return;                              // gone: the chip is rebooting into its bootloader
            uint32_t got = 0;
            fIn->io(fRx, 13, got, 1000);             // CSW
        }
        if (round == 0)
            LDLOG("eject sent");
        IOSleep(200);
    }
    if (!fStop)
        LDLOG("device ignored eject");
}

void AicLoader::upload()
{
    for (uint32_t i = 0; i < kAICLoaderSeqCount && !fStop; i++) {
        const AICLoaderFrame &f = kAICLoaderSeq[i];
        if (!write(kAICLoaderBlob + f.off, f.len)) {
            LDLOG("frame %u (0x%04x): write failed", i, f.id);
            return;
        }
        if (f.id == 0x040d) {                        // DBG_START_APP: the chip reboots into the firmware
            LDLOG("firmware uploaded (%u frames), starting it", kAICLoaderSeqCount);
            return;
        }
        // Wait for <id>+1. Messages: u16 len, u8 type 0x11, u8, then ipc_e2a_msg {id, dest, src, param_len, u32, params}
        bool ok = false;
        for (int tries = 0; tries < 4 && !ok && !fStop; tries++) {
            uint32_t got = 0;
            if (fIn->io(fRx, kBuf, got, 2000) != kIOReturnSuccess)
                continue;
            const uint8_t *b = (const uint8_t *)fRx->getBytesNoCopy();
            // block-write CFMs are 20 bytes, read CFMs 24
            if (got < 16 || (b[2] & 0x7f) != 0x11 || (uint16_t)(b[4] | (b[5] << 8)) != f.id + 1)
                continue;
            if (f.hasExpect && (got < 24 || memcmp(b + 16, f.expect, 8) != 0)) {
                // Only the chip ID (0x40500000) identifies the chip; other reads are state, e.g. 0x40100020
                // reads 3 instead of 2 when an earlier upload attempt already wrote it.
                bool chipId = f.expect[0] == 0x00 && f.expect[1] == 0x00 && f.expect[2] == 0x50 && f.expect[3] == 0x40;
                LDLOG("frame %u: read %02x%02x%02x%02x = %02x%02x%02x%02x, capture differs%s", i,
                      b[19], b[18], b[17], b[16], b[23], b[22], b[21], b[20], chipId ? "; not this chip, stopping" : "");
                if (chipId)
                    return;
            }
            ok = true;
        }
        if (!ok) {
            LDLOG("frame %u (0x%04x): no confirmation", i, f.id);
            return;
        }
    }
}
