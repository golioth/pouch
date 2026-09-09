// Package gateway runs Pouch sync sessions between a device on a frame link and
// a Pouch-compatible cloud.
package gateway

import (
	"context"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"log/slog"
	"sync"
	"time"

	"github.com/fxamacker/cbor/v2"

	"github.com/golioth/pouch/examples/linux/rpmsg_gateway/internal/serial"
)

// infoFlagDeviceProvisioned is set by the device once the cloud holds its
// certificate. The remaining bits are the capability exchange.
const infoFlagDeviceProvisioned = 1 << 0

// infoFlagFwSignedURL is set by a device built with the signed-URL handoff. It
// is what tells us the time and URL channels exist on the other end.
const infoFlagFwSignedURL = 1 << 1

// deviceInfo is the CBOR map the device sends on the INFO channel.
type deviceInfo struct {
	Flags         uint8  `cbor:"flags"`
	ServerCertSNR []byte `cbor:"server_cert_snr"`
}

// Link is a frame-oriented transport to the device.
type Link interface {
	ReadFrame() ([]byte, error)
	WriteFrame(frame []byte) error
	Close() error
}

// Cloud is the Pouch server contract.
type Cloud interface {
	ServerCert(ctx context.Context) ([]byte, error)
	RegisterDevice(ctx context.Context, cert []byte) error
	Forward(ctx context.Context, uplink []byte) ([]byte, error)
}

// Gateway brokers sessions for one device.
type Gateway struct {
	link  Link
	cloud Cloud
	log   *slog.Logger

	maxFrame int

	// baseCtx outlives any one session, so a download started by one can
	// finish after it. Set by New from the process context.
	baseCtx context.Context

	// Firmware carries MCU-mediated updates when set. relay persists across
	// sessions because the image arrives a chunk at a time, and downloads
	// because an artifact takes longer to fetch than a session lasts.
	Firmware  *FirmwareOptions
	relay     fwRelay
	downloads fwDownloads

	// serverCert is fetched once and reused across sessions.
	certOnce sync.Once
	cert     []byte
	certSNR  []byte
	certErr  error
}

// FirmwareOptions enables MCU-mediated firmware relay. A core loaded by
// remoteproc has no flash of its own, so it downloads its own image through
// Pouch OTA and streams the plaintext here for the host to verify and apply.
type FirmwareOptions struct {
	// Apply installs a verified image. Leaving it nil verifies and discards,
	// which is what a bring-up run wants.
	Apply func(pkg, version string, image []byte) error
	// ForceReject reports a hash failure for an image that actually verified.
	// It exists to exercise the device's retry path on demand.
	ForceReject bool

	// SignedURL accepts a signed artifact URL from the device and fetches the
	// image ourselves, rather than having it relayed through the device.
	SignedURL bool
	// Downloader fetches those artifacts. Required when SignedURL is set.
	Downloader *Downloader
	// ApplyFile installs an artifact already on disk. Leaving it nil verifies
	// and discards, which is what a bring-up run wants.
	ApplyFile func(pkg, version, path string) error
}

// New returns a gateway that brokers between link and cloud.
func New(ctx context.Context, link Link, cloud Cloud, maxFrame int, log *slog.Logger) *Gateway {
	return &Gateway{baseCtx: ctx, link: link, cloud: cloud, maxFrame: maxFrame, log: log}
}

// serverCert fetches the chain to provision, and the serial number the device
// reports back once it holds it.
func (g *Gateway) serverCert(ctx context.Context) ([]byte, []byte, error) {
	g.certOnce.Do(func() {
		g.cert, g.certErr = g.cloud.ServerCert(ctx)
		if g.certErr != nil {
			return
		}
		// The device echoes the serial of the certificate it holds, which is how
		// the broker decides whether provisioning can be skipped next time. The
		// chain may hold several; the device reports the first, matching
		// mbedtls_x509_crt_parse().
		leaf, err := firstCertificate(g.cert)
		if err != nil {
			g.log.Warn("cannot derive the server certificate serial, "+
				"so the chain will be pushed every session", "err", err)
		} else {
			g.certSNR = derSerial(leaf)
		}
	})
	return g.cert, g.certSNR, g.certErr
}

// RunSession brokers one complete sync and returns when it finishes.
func (g *Gateway) RunSession(ctx context.Context) error {
	cert, certSNR, err := g.serverCert(ctx)
	if err != nil {
		return fmt.Errorf("fetch server certificate: %w", err)
	}

	var (
		sessErr    error
		finished   bool
		uplinkDone bool
		deviceCert []byte
		uplink     []byte
		downlink   []byte
		fwVerdict  FwStatus
	)

	ep := serial.Endpoints{}
	var broker *serial.Broker

	// INFO: the device tells us what it already has.
	ep.Info = &serial.BufReceiver{Done: func(data []byte, ok bool) {
		if !ok {
			return
		}
		var info deviceInfo
		if err := cbor.Unmarshal(data, &info); err != nil {
			sessErr = fmt.Errorf("decode device info: %w", err)
			return
		}
		node := broker.Node()
		node.ServerCertProvisioned = len(certSNR) > 0 &&
			string(info.ServerCertSNR) == string(certSNR)
		node.DeviceCertProvisioned = info.Flags&infoFlagDeviceProvisioned != 0
		node.SignedURLCapable = info.Flags&infoFlagFwSignedURL != 0 &&
			g.Firmware != nil && g.Firmware.SignedURL

		g.log.Info("device info",
			"flags", info.Flags,
			"server_cert_provisioned", node.ServerCertProvisioned,
			"device_cert_provisioned", node.DeviceCertProvisioned,
			"signed_url", node.SignedURLCapable)
	}}

	// SERVER_CERT: push the chain so the device can authenticate the cloud.
	ep.ServerCert = &serial.BufSender{
		Data: func() []byte { return cert },
		Done: func(ok bool) {
			if ok {
				broker.Node().ServerCertProvisioned = true
				g.log.Info("server certificate provisioned", "bytes", len(cert))
			}
		},
	}

	// DEVICE_CERT: collect the device's leaf and register it with the cloud.
	ep.DeviceCert = &serial.BufReceiver{Done: func(data []byte, ok bool) {
		if !ok {
			return
		}
		deviceCert = append([]byte(nil), data...)
		if err := g.cloud.RegisterDevice(ctx, deviceCert); err != nil {
			sessErr = fmt.Errorf("register device: %w", err)
			return
		}
		broker.Node().DeviceCertProvisioned = true
		g.log.Info("device certificate registered", "bytes", len(deviceCert))
	}}

	// UPLINK: collect the outbound pouch and post it; the reply is the downlink.
	ep.Uplink = &serial.BufReceiver{Done: func(data []byte, ok bool) {
		if !ok {
			return
		}
		uplink = append([]byte(nil), data...)
		uplinkDone = true
		g.log.Info("uplink collected", "bytes", len(uplink))
	}}

	// DOWNLINK: post the uplink and hand back what the cloud returns.
	//
	// Both directions are opened in the same phase, so the device usually
	// prompts for its downlink before it has finished sending the uplink. There
	// is nothing to post yet at that point - and an empty body is not a valid
	// pouch, the server rejects it with 400 - so the sender reports "not ready"
	// until the uplink transfer closes. Zero bytes with MoreData re-arms the
	// channel without putting a frame on the wire, which is exactly what that
	// result is for.
	ep.Downlink = &deferredSender{
		ready: func() bool { return uplinkDone },
		fetch: func() ([]byte, error) {
			resp, err := g.cloud.Forward(ctx, uplink)
			if err != nil {
				return nil, err
			}
			downlink = resp
			g.log.Info("downlink received", "bytes", len(downlink))
			return downlink, nil
		},
		fail: func(err error) { sessErr = fmt.Errorf("forward uplink: %w", err) },
	}

	if g.Firmware != nil {
		ep.Fw = &serial.BufReceiver{Done: func(data []byte, ok bool) {
			// An empty transfer is the normal answer: it means the device has
			// no image in flight.
			if !ok || len(data) == 0 {
				return
			}
			chunk, err := parseFwChunk(data)
			if err != nil {
				g.log.Error("firmware relay", "err", err)
				g.relay.reset()
				return
			}
			complete, err := g.relay.push(chunk)
			if err != nil {
				g.log.Error("firmware relay", "err", err)
				g.relay.reset()
				return
			}
			g.log.Info("firmware chunk",
				"package", chunk.pkg, "version", chunk.version,
				"offset", chunk.offset, "bytes", len(chunk.data),
				"have", len(g.relay.image), "of", g.relay.size)

			if !complete {
				return
			}

			fwVerdict = g.relay.verdict()
			if fwVerdict == FwStatusOK && g.Firmware.ForceReject {
				g.log.Warn("image verified, reporting a hash failure anyway " +
					"to exercise the device's retry path")
				fwVerdict = FwStatusHashFail
			}
			if fwVerdict == FwStatusOK && g.Firmware.Apply != nil {
				if err := g.Firmware.Apply(g.relay.pkg, g.relay.version, g.relay.image); err != nil {
					g.log.Error("apply firmware", "err", err)
					fwVerdict = FwStatusError
				}
			}
			g.log.Info("firmware image complete",
				"package", g.relay.pkg, "version", g.relay.version,
				"bytes", len(g.relay.image), "verdict", fwVerdict)

			g.relay.reset()
			broker.SendFwStatus()
		}}

		ep.FwStatus = &serial.BufSender{
			Data: func() []byte { return []byte{byte(fwVerdict)} },
			Done: func(ok bool) {
				if ok {
					g.log.Info("apply verdict delivered", "verdict", fwVerdict)
				}
			},
		}
	}

	if g.Firmware != nil && g.Firmware.SignedURL {
		// The device has no clock of its own, and signs against a validity
		// window, so it needs ours before it can sign anything.
		ep.Time = &serial.BufSender{
			Data: func() []byte { return timeRecord(time.Now()) },
			Done: func(ok bool) {
				if ok {
					g.log.Debug("reported the time to the device")
				}
			},
		}

		ep.FwURL = &serial.BufReceiver{Done: func(data []byte, ok bool) {
			// An empty transfer is the normal answer: no artifact pending.
			if !ok || len(data) == 0 {
				return
			}
			rec, err := parseFwURLRecord(data)
			if err != nil {
				g.log.Error("signed-URL handoff", "err", err)
				return
			}
			// Debug only: the URL carries a short-lived signature, and seeing
			// the exact bytes the device produced is the only way to tell a
			// refused signature from a malformed one.
			g.log.Debug("signed artifact URL", "url", rec.url)

			// Started here and finished long after this session ends: an
			// artifact takes minutes, and holding the session open would stall
			// the uplink and downlink behind a file transfer.
			g.startDownload(g.baseCtx, rec)
		}}
	}

	broker = serial.NewBroker(ep, g.log,
		func(ok bool) {
			finished = true
			if !ok && sessErr == nil {
				sessErr = fmt.Errorf("session ended in error")
			}
		},
		func() {},
	)

	broker.Start()

	// A download that finished between sessions is reported now. The device is
	// waiting on this verdict: it told the cloud to stop offering the component
	// when it handed the URL over, so nothing else will remind it.
	if res := g.takeDownloadResult(); res != nil {
		fwVerdict = res.verdict

		if fwVerdict == FwStatusOK && g.Firmware.ForceReject {
			g.log.Warn("artifact verified, reporting a hash failure anyway " +
				"to exercise the device's retry path")
			fwVerdict = FwStatusHashFail
		}
		if fwVerdict == FwStatusOK && g.Firmware.ApplyFile != nil {
			if err := g.Firmware.ApplyFile(res.pkg, res.version, res.path); err != nil {
				g.log.Error("apply artifact", "err", err)
				fwVerdict = FwStatusError
			}
		}

		g.log.Info("reporting the artifact verdict",
			"package", res.pkg, "version", res.version, "verdict", fwVerdict)
		broker.SendFwStatus()
	}

	// Pump: drain everything the broker wants to say, then block for a frame.
	for !finished {
		for {
			frame := broker.FrameGet(g.maxFrame)
			if frame == nil {
				break
			}
			if err := g.link.WriteFrame(frame); err != nil {
				return err
			}
		}
		if finished {
			break
		}

		frame, err := g.link.ReadFrame()
		if err != nil {
			return err
		}
		if err := broker.Recv(frame); err != nil {
			// A frame the broker rejects is not fatal to the link; log it and
			// keep going so a stray frame cannot end the session.
			g.log.Warn("dropped frame", "err", err)
		}
	}

	return sessErr
}

// derSerial returns the serial number as DER encodes its content octets, which
// is the form the device reports. A positive integer whose top bit is set gains
// a leading zero byte, and Go's big.Int does not carry it.
func derSerial(c *x509.Certificate) []byte {
	b := c.SerialNumber.Bytes()
	if len(b) > 0 && b[0]&0x80 != 0 {
		return append([]byte{0x00}, b...)
	}
	return b
}

// deferredSender serves a payload that is not available when the transfer
// opens. It reports MoreData with no bytes until ready() is true, which re-arms
// the channel without emitting a frame.
type deferredSender struct {
	ready func() bool
	fetch func() ([]byte, error)
	fail  func(error)

	data    []byte
	off     int
	fetched bool
}

func (s *deferredSender) Start() error {
	s.data, s.off, s.fetched = nil, 0, false
	return nil
}

func (s *deferredSender) Send(max int) ([]byte, serial.Result) {
	if !s.fetched {
		if !s.ready() {
			return nil, serial.MoreData
		}
		data, err := s.fetch()
		if err != nil {
			if s.fail != nil {
				s.fail(err)
			}
			return nil, serial.SendError
		}
		s.data, s.fetched = data, true
	}

	n := min(len(s.data)-s.off, max)
	chunk := s.data[s.off : s.off+n]
	s.off += n
	if s.off >= len(s.data) {
		return chunk, serial.NoMoreData
	}
	return chunk, serial.MoreData
}

func (s *deferredSender) End(bool) {}

// firstCertificate returns the leading certificate of a chain, which the server
// may hand over as PEM or as concatenated DER.
func firstCertificate(chain []byte) (*x509.Certificate, error) {
	if block, _ := pem.Decode(chain); block != nil {
		if block.Type != "CERTIFICATE" {
			return nil, fmt.Errorf("first PEM block is %q, not CERTIFICATE", block.Type)
		}
		return x509.ParseCertificate(block.Bytes)
	}

	certs, err := x509.ParseCertificates(chain)
	if err != nil {
		return nil, err
	}
	if len(certs) == 0 {
		return nil, fmt.Errorf("no certificates in chain")
	}
	return certs[0], nil
}
