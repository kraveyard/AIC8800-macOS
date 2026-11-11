// AIC8800D80 USB Wi-Fi driver for macOS - stage 2: command channel + scan.
// Protocol: docs/PROTOCOL.md. Init frames: AICInitSeq.h (generated from a Linux usbmon capture).

#include <IOKit/IOService.h>
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>
#include <kern/thread.h>
#include <libkern/OSAtomic.h>

#include "AICInitSeq.h"

#define LOG(fmt, ...) IOLog("AIC8800: " fmt "\n", ##__VA_ARGS__)

static const uint8_t  kEpCmdOut = 0x02;
static const uint8_t  kEpCmdIn  = 0x82;
static const uint32_t kRxSize   = 4096;
static const uint16_t kScanuStartReq = 0x1000, kScanuStartCfm = 0x1001, kScanuResultInd = 0x1004;

class AIC8800USB : public IOService {
    OSDeclareDefaultStructors(AIC8800USB)

public:
    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

private:
    IOUSBHostInterface       *fIntf    = nullptr;
    IOUSBHostPipe            *fCmdOut  = nullptr;
    IOUSBHostPipe            *fCmdIn   = nullptr;
    IOBufferMemoryDescriptor *fTxBuf   = nullptr;
    IOBufferMemoryDescriptor *fRxBuf   = nullptr;
    volatile SInt32           fStop    = 0;
    volatile SInt32           fRunning = 0;

    static void initThread(void *arg, wait_result_t);
    void runInitAndScan();
    bool sendFrame(const uint8_t *data, uint32_t len);
    // Waits for the confirmation of request `id`; logs scan results along the way.
    bool waitFor(uint16_t id, uint32_t timeoutMs, uint32_t *bssCount);
    void logScanResult(const uint8_t *p, uint16_t plen);
    void releaseAll();
};

OSDefineMetaClassAndStructors(AIC8800USB, IOService)

bool AIC8800USB::start(IOService *provider)
{
    if (!IOService::start(provider))
        return false;

    fIntf = OSDynamicCast(IOUSBHostInterface, provider);
    if (!fIntf || !fIntf->open(this)) {
        LOG("cannot open interface");
        return false;
    }
    fIntf->retain();

    fCmdOut = fIntf->copyPipe(kEpCmdOut);
    fCmdIn  = fIntf->copyPipe(kEpCmdIn);
    fTxBuf  = fIntf->createIOBuffer(kIODirectionOut, kRxSize);
    fRxBuf  = fIntf->createIOBuffer(kIODirectionIn, kRxSize);
    if (!fCmdOut || !fCmdIn || !fTxBuf || !fRxBuf) {
        LOG("pipe/buffer setup failed (out=%p in=%p)", fCmdOut, fCmdIn);
        releaseAll();
        return false;
    }

    thread_t th = nullptr;
    retain();  // released by the thread
    OSIncrementAtomic(&fRunning);
    if (kernel_thread_start(initThread, this, &th) != KERN_SUCCESS) {
        OSDecrementAtomic(&fRunning);
        release();
        releaseAll();
        return false;
    }
    thread_deallocate(th);

    LOG("attached, interface 2 open");
    registerService();
    return true;
}

void AIC8800USB::stop(IOService *provider)
{
    OSIncrementAtomic(&fStop);
    if (fCmdIn)  fCmdIn->abort();
    if (fCmdOut) fCmdOut->abort();
    for (int i = 0; i < 100 && fRunning; i++)  // up to 5s for the init thread to leave
        IOSleep(50);
    releaseAll();
    IOService::stop(provider);
}

void AIC8800USB::releaseAll()
{
    OSSafeReleaseNULL(fCmdOut);
    OSSafeReleaseNULL(fCmdIn);
    OSSafeReleaseNULL(fTxBuf);
    OSSafeReleaseNULL(fRxBuf);
    if (fIntf) {
        fIntf->close(this);
        OSSafeReleaseNULL(fIntf);
    }
}

void AIC8800USB::initThread(void *arg, wait_result_t)
{
    AIC8800USB *self = static_cast<AIC8800USB *>(arg);
    self->runInitAndScan();
    OSDecrementAtomic(&self->fRunning);
    self->release();
    thread_terminate(current_thread());
}

bool AIC8800USB::sendFrame(const uint8_t *data, uint32_t len)
{
    if (len > kRxSize)
        return false;
    memcpy(fTxBuf->getBytesNoCopy(), data, len);
    uint32_t done = 0;
    IOReturn r = fCmdOut->io(fTxBuf, len, done, 1000);
    if (r != kIOReturnSuccess || done != len) {
        LOG("tx failed 0x%x (%u/%u)", r, done, len);
        return false;
    }
    return true;
}

bool AIC8800USB::waitFor(uint16_t id, uint32_t timeoutMs, uint32_t *bssCount)
{
    const uint16_t want = (id == kScanuStartReq) ? kScanuStartCfm : (uint16_t)(id + 1);
    uint64_t deadline;
    clock_interval_to_deadline(timeoutMs, kMillisecondScale, &deadline);

    while (!fStop && mach_absolute_time() < deadline) {
        uint32_t got = 0;
        IOReturn r = fCmdIn->io(fRxBuf, kRxSize, got, 300);
        if (r == kIOReturnTimeout)
            continue;
        if (r != kIOReturnSuccess) {
            LOG("rx failed 0x%x", r);
            return false;
        }
        const uint8_t *b = (const uint8_t *)fRxBuf->getBytesNoCopy();
        // Several messages may share one transfer; each is 4-byte padded.
        for (uint32_t off = 0; off + 4 <= got;) {
            uint32_t plen = b[off] | ((b[off + 1] & 0x0f) << 8);
            uint8_t type = b[off + 2];
            if (plen == 0 || off + 4 + plen > got)
                break;
            const uint8_t *body = b + off + 4;
            if ((type & 0x7f) == 0x11 && plen >= 12) {
                uint16_t mid  = body[0] | (body[1] << 8);
                uint16_t mlen = body[6] | (body[7] << 8);
                const uint8_t *param = body + 12;
                if (12u + mlen > plen)
                    mlen = (uint16_t)(plen - 12);
                if (mid == kScanuResultInd) {
                    logScanResult(param, mlen);
                    if (bssCount) (*bssCount)++;
                } else if (mid == want) {
                    if (id == 0x0080 && mlen > 1)   // MM_GET_FW_VERSION_CFM: u8 len + string
                        LOG("fw version: %.*s", param[0] < mlen ? param[0] : mlen - 1, param + 1);
                    if (id == 0x0073 && mlen >= 6)  // MM_GET_MAC_ADDR_CFM
                        LOG("mac: %02x:%02x:%02x:%02x:%02x:%02x",
                            param[0], param[1], param[2], param[3], param[4], param[5]);
                    return true;
                }
            }
            off += (plen + 4 + 3) & ~3u;
        }
    }
    return false;
}

void AIC8800USB::logScanResult(const uint8_t *p, uint16_t plen)
{
    if (plen < 12)
        return;
    uint16_t flen = p[0] | (p[1] << 8);
    uint16_t freq = p[4] | (p[5] << 8);
    int8_t rssi = (int8_t)p[9];  // u16 len, u16 fc, u16 freq, u8 band, u8 sta, u8 inst, s8 rssi, 2 pad
    const uint8_t *fr = p + 12;
    if (12u + flen > plen || flen < 36)
        return;
    char ssid[33] = "<hidden>";
    // 24-byte mgmt header + 12 fixed fields, then IEs; SSID is IE 0.
    for (uint32_t i = 36; i + 2 <= flen;) {
        uint8_t eid = fr[i], ln = fr[i + 1];
        if (i + 2 + ln > flen)
            break;
        if (eid == 0) {
            if (ln > 0 && ln <= 32) {
                memcpy(ssid, fr + i + 2, ln);
                ssid[ln] = 0;
            }
            break;
        }
        i += 2 + ln;
    }
    LOG("bss %02x:%02x:%02x:%02x:%02x:%02x %4d dBm %5u MHz %s",
        fr[16], fr[17], fr[18], fr[19], fr[20], fr[21], rssi, freq, ssid);
}

void AIC8800USB::runInitAndScan()
{
    uint32_t bss = 0;
    for (uint32_t i = 0; i < kAICInitSeqCount && !fStop; i++) {
        const AICInitFrame &f = kAICInitSeq[i];
        if (!sendFrame(f.data, f.len)) {
            LOG("abort at frame %u (id 0x%04x): send", i, f.id);
            return;
        }
        if (!waitFor(f.id, f.id == kScanuStartReq ? 12000 : 2000, &bss)) {
            LOG("abort at frame %u (id 0x%04x): no confirmation", i, f.id);
            return;
        }
    }
    if (!fStop)
        LOG("init + scan done, %u results", bss);
}
