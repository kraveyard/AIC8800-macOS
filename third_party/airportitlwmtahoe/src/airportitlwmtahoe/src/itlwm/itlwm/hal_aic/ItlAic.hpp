/*
 * AIC8800D80 (USB, fullmac) backend for AirportItlwm-Tahoe.
 * GPL-2.0, like the itlwm code it plugs into.
 *
 * Protocol: AIC8800-macOS/docs/PROTOCOL.md. The firmware does 802.11 auth/assoc and crypto;
 * net80211 keeps the node table, the state machine and the WPA2 supplicant.
 */
#ifndef ItlAic_hpp
#define ItlAic_hpp

#include <compat.h>
#include <linux/kernel.h>
#include <sys/param.h>
#include <sys/mbuf.h>
#include <sys/kpi_mbuf.h>
#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_priv.h>
#include <HAL/ItlHalService.hpp>
#include <HAL/ItlDriverInfo.hpp>
#include <HAL/ItlDriverController.hpp>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IOLocks.h>
#include <IOKit/usb/IOUSBHostInterface.h>
#include <IOKit/usb/IOUSBHostPipe.h>

struct aic_softc {
    struct ieee80211com sc_ic;   // must stay first: net80211 casts the ifnet to the ic
    int (*sc_newstate)(struct ieee80211com *, enum ieee80211_state, int);
};

// One firmware message or data frame handed from a USB reader thread to the workloop.
struct AicRxPkt {
    AicRxPkt *next;
    uint32_t len;
    uint16_t msgId;      // 0 for a data frame
    uint8_t data[];      // msg: lmac params; data: 60-byte rx header + 802.11 frame
};

class ItlAic : public ItlHalService, ItlDriverInfo, ItlDriverController {
    OSDeclareDefaultStructors(ItlAic)

public:
    // Set by AicUSBDeviceWrapper::probe(), before AirportItlwm::start() calls attach().
    void setUSBInterface(IOUSBHostInterface *intf) { fIntf = intf; }

    // USB transport and firmware init, run by AicUSBDeviceWrapper on its own thread *before* the wrapper
    // registers, so AirportItlwm::start() (and boot, which waits for the registry to go quiet) never
    // blocks on firmware commands. attach() then only hooks the workloop and the ieee80211com.
    bool bringUp();
    // Any thread: make every pending and future USB transfer and command wait fail at once (unplug).
    void abortTransport();
    // Stop reader threads and release pipes; idempotent. detach() calls it too.
    void shutdownTransport();

    // ItlHalService
    bool attach(IOPCIDevice *unused) override;
    void detach(IOPCIDevice *unused) override;
    IOReturn enable(IONetworkInterface *netif) override;
    IOReturn disable(IONetworkInterface *netif) override;
    struct ieee80211com *get80211Controller() override { return &com.sc_ic; }
    ItlDriverInfo *getDriverInfo() override { return this; }
    ItlDriverController *getDriverController() override { return this; }
    void free() override;

    // ItlDriverInfo
    const char *getFirmwareVersion() override { return fFwVersion; }
    int16_t getBSSNoise() override { return -95; }
    bool is5GBandSupport() override { return true; }
    int getTxNSS() override { return 1; }
    const char *getFirmwareName() override { return "AIC8800D80"; }
    UInt32 supportedFeatures() override { return 0; }
    const char *getFirmwareCountryCode() override { return "ZZ"; }
    uint32_t getTxQueueSize() override { return 256; }

    // ItlDriverController
    void clearScanningFlags() override { fScanning = false; }
    IOReturn setMulticastList(IOEthernetAddress *, int) override { return kIOReturnSuccess; }

private:
    struct aic_softc com;
    IOUSBHostInterface *fIntf = nullptr;
    IOUSBHostPipe *fCmdOut = nullptr, *fCmdIn = nullptr, *fDataOut = nullptr, *fDataIn = nullptr;
    IOBufferMemoryDescriptor *fCmdTxBuf = nullptr, *fCmdRxBuf = nullptr, *fDataTxBuf = nullptr, *fDataRxBuf = nullptr;
    IOInterruptEventSource *volatile fRxSource = nullptr;   // published by attach(), read by reader threads
    IOLock *fPipeLock = nullptr;     // pipe pointers: abortTransport() vs closePipes()
    bool fTransportUp = false;
    IOLock *fCmdLock = nullptr;      // serialises command senders
    IOLock *fCfmLock = nullptr;      // protects the confirmation slot and the rx queue
    AicRxPkt *fRxHead = nullptr, *fRxTail = nullptr;
    uint32_t fRxCount = 0;

    volatile SInt32 fStopping = 0, fThreads = 0;
    uint16_t fWaitId = 0;            // CFM id a sender waits for, 0 = none
    bool fWaitDone = false;
    uint8_t fCfm[64];
    uint16_t fCfmLen = 0;

    char fFwVersion[64] = "unknown";
    uint8_t fVifMac[6] = {};         // address the firmware vif was created with
    uint8_t fVifIdx = 0, fApIdx = 0;
    bool fScanning = false;
    bool fConnecting = false;        // SM_CONNECT_REQ sent, no SM_CONNECT_IND yet
    bool fFwConnected = false;       // firmware holds an association
    bool fDisconnecting = false;     // we asked the firmware to leave
    bool fPortOpen = false;
    int fHwKeyPairwise = -1, fHwKeyGroup[4] = {-1, -1, -1, -1};
    mbuf_t fPendingEapol = nullptr;  // EAPOL that beat SM_CONNECT_IND through the other pipe
    uint32_t fDbgTx = 0, fDbgRx = 0; // shortcut: debug log budget for the first frames, remove once data path is proven

    // transport
    bool openPipes();
    void closePipes();
    void abortPipes();
    void drainRxQueue();
    static void rxThreadEntry(void *arg, wait_result_t);
    void rxLoop(bool dataPipe);
    void parseTransfer(const uint8_t *b, uint32_t len);
    void queueRx(uint16_t msgId, const uint8_t *data, uint32_t len);
    static void rxSourceAction(OSObject *owner, IOInterruptEventSource *, int);
    bool usbWrite(IOUSBHostPipe *pipe, IOBufferMemoryDescriptor *buf, uint32_t len);
    int sendMsg(uint16_t id, const void *param, uint16_t plen, uint16_t cfmId,
                void *cfmOut = nullptr, uint16_t cfmMax = 0, uint32_t timeoutMs = 2000);
    int sendRawFrame(const uint8_t *frame, uint32_t len, uint16_t cfmId,
                     void *cfmOut, uint16_t cfmMax, uint32_t timeoutMs);
    int waitCfm(uint16_t cfmId, void *cfmOut, uint16_t cfmMax, uint32_t timeoutMs);
    void handleMsg(uint16_t id, const uint8_t *p, uint32_t plen);   // workloop
    void handleData(const uint8_t *p, uint32_t len);                // workloop

    // device bring-up
    bool firmwareInit();
    int addVif(const uint8_t *mac);
    void setupIC();

    // net80211 glue
    int aicInit(struct _ifnet *ifp);
    void aicStop(struct _ifnet *ifp);
    int startScan();
    int startConnect(struct ieee80211_node *ni);
    void fwDisconnect();
    void deleteHwKeys();
    int setControlPort(bool open);
    mbuf_t makeMbuf(const uint8_t *data, uint32_t len);
    void inputFrame(mbuf_t m, int rssi, uint16_t freq, bool hwdec, bool needReord = false);
    // A-MPDU RX reordering (the firmware negotiates ADDBA; net80211 never sees it)
    bool rxBaStart(struct ieee80211_node *ni, uint8_t tid, uint16_t ssn);
    void rxBaStop(struct ieee80211_node *ni, uint8_t tid, bool deliver);
    void rxBaStopAll(bool deliver);
    void injectMgmt(uint8_t subtype, const uint8_t *body, uint32_t blen);
    void onConnectInd(const uint8_t *p, uint32_t plen);
    void onDisconnectInd(const uint8_t *p, uint32_t plen);
    void txData(mbuf_t m);

    static int aic_newstate(struct ieee80211com *, enum ieee80211_state, int);
    static void aic_start(struct _ifnet *);
    static IOReturn aic_start_task(OSObject *, void *, void *, void *, void *);
    static int aic_ioctl(struct _ifnet *, u_long, caddr_t);
    static void aic_watchdog(struct _ifnet *ifp) { ifp->if_timer = 0; }
    static int aic_set_key(struct ieee80211com *, struct ieee80211_node *, struct ieee80211_key *);
    static void aic_delete_key(struct ieee80211com *, struct ieee80211_node *, struct ieee80211_key *);
    static ItlAic *fromIC(struct ieee80211com *ic) { return container_of((struct aic_softc *)ic, ItlAic, com); }
};

#endif /* ItlAic_hpp */
