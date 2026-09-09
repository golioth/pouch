package gateway

import (
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"os"
	"path/filepath"
	"sync"
	"time"
)

// Downloader fetches an artifact from a URL the device signed.
//
// The signature is the only credential involved: it authorizes this one
// artifact for a short window, so the request carries no identity of ours. That
// is deliberate - presenting the gateway's client certificate to whatever host
// serves the artifact would hand our identity to a third party for no benefit.
type Downloader struct {
	// Client fetches the artifact. Left nil, a default one is built with the
	// system roots.
	Client *http.Client
	// Dir is where verified artifacts land.
	Dir string
	// Timeout bounds one download. A whole-request deadline on the client
	// would be wrong here: artifacts run to megabytes over links we do not
	// control, so the bound is applied per download and set generously.
	Timeout time.Duration
	Log     *slog.Logger
}

// NewHTTPClient returns a client suitable for artifact downloads. caFile, if
// given, is a PEM bundle trusted in addition to the system roots.
func NewHTTPClient(caFile string) (*http.Client, error) {
	var roots *x509.CertPool

	if caFile != "" {
		pem, err := os.ReadFile(caFile)
		if err != nil {
			return nil, fmt.Errorf("read CA bundle: %w", err)
		}
		roots, err = x509.SystemCertPool()
		if err != nil {
			roots = x509.NewCertPool()
		}
		if !roots.AppendCertsFromPEM(pem) {
			return nil, fmt.Errorf("no certificates found in %s", caFile)
		}
	}

	return &http.Client{
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{
				MinVersion: tls.VersionTLS12,
				RootCAs:    roots,
			},
			ResponseHeaderTimeout: 30 * time.Second,
		},
		// A redirect out of https would move the artifact, and the signature
		// that authorizes it, onto a cleartext connection.
		CheckRedirect: func(req *http.Request, via []*http.Request) error {
			if req.URL.Scheme != "https" {
				return fmt.Errorf("refusing redirect to %q", req.URL.Scheme)
			}
			if len(via) >= 10 {
				return errors.New("too many redirects")
			}
			return nil
		},
	}, nil
}

// downloadResult is what one completed download has to say for itself.
type downloadResult struct {
	pkg     string
	version string
	path    string
	verdict FwStatus
}

// Fetch downloads and verifies one artifact. It reports a verdict rather than
// an error for anything the device needs to hear about, because the verdict is
// what goes back over the wire.
func (d *Downloader) Fetch(ctx context.Context, rec *fwURLRecord) downloadResult {
	result := downloadResult{pkg: rec.pkg, version: rec.version, verdict: FwStatusError}

	ctx, cancel := context.WithTimeout(ctx, d.Timeout)
	defer cancel()

	req, err := http.NewRequestWithContext(ctx, http.MethodGet, rec.url, nil)
	if err != nil {
		d.Log.Error("build artifact request", "err", err)
		return result
	}

	resp, err := d.Client.Do(req)
	if err != nil {
		d.Log.Error("fetch artifact", "err", err)
		return result
	}
	defer func() { _ = resp.Body.Close() }()

	if resp.StatusCode != http.StatusOK {
		// 401 or 403 here is the usual sign that the project does not have
		// signed URLs enabled, or that the device's CA was never uploaded to
		// it. The device falls back to relaying when it hears this.
		d.Log.Error("artifact server refused the signed URL",
			"status", resp.Status, "package", rec.pkg, "version", rec.version)
		return result
	}

	if resp.ContentLength >= 0 && resp.ContentLength != int64(rec.size) {
		d.Log.Error("artifact is not the size the manifest promised",
			"got", resp.ContentLength, "want", rec.size)
		return result
	}

	if err := os.MkdirAll(d.Dir, 0o755); err != nil {
		d.Log.Error("create download directory", "err", err)
		return result
	}

	final := filepath.Join(d.Dir, fmt.Sprintf("%s-%s.elf", rec.pkg, rec.version))
	partial := final + ".part"

	f, err := os.Create(partial)
	if err != nil {
		d.Log.Error("create artifact file", "err", err)
		return result
	}

	digest := sha256.New()
	// One byte past the promised size, so an artifact that is too long is seen
	// as too long rather than silently truncated to a matching length.
	n, err := io.Copy(io.MultiWriter(f, digest), io.LimitReader(resp.Body, int64(rec.size)+1))
	closeErr := f.Close()

	if err != nil || closeErr != nil {
		d.Log.Error("write artifact", "err", errors.Join(err, closeErr))
		_ = os.Remove(partial)
		return result
	}

	if n != int64(rec.size) {
		d.Log.Error("artifact is not the size the manifest promised",
			"got", n, "want", rec.size)
		_ = os.Remove(partial)
		return result
	}

	var got [32]byte
	copy(got[:], digest.Sum(nil))
	if got != rec.sha256 {
		d.Log.Error("artifact does not match the manifest digest",
			"package", rec.pkg, "version", rec.version)
		_ = os.Remove(partial)
		result.verdict = FwStatusHashFail
		return result
	}

	if err := os.Rename(partial, final); err != nil {
		d.Log.Error("install artifact", "err", err)
		_ = os.Remove(partial)
		return result
	}

	d.Log.Info("artifact downloaded and verified",
		"package", rec.pkg, "version", rec.version, "bytes", n, "path", final)

	result.path = final
	result.verdict = FwStatusOK
	return result
}

// fwDownloads tracks the one download that may be in flight.
//
// The download outlives the session that asked for it: artifacts take longer
// than a session does, and holding the session open would stall the uplink and
// downlink behind a file transfer. The verdict therefore goes back on a later
// session, which is what the device's apply-verdict channel is already shaped
// for.
type fwDownloads struct {
	mu       sync.Mutex
	inflight *fwURLRecord
	cancel   context.CancelFunc
	result   *downloadResult
}

// start begins a download unless the same artifact is already being fetched.
func (g *Gateway) startDownload(ctx context.Context, rec *fwURLRecord) {
	g.downloads.mu.Lock()
	defer g.downloads.mu.Unlock()

	if cur := g.downloads.inflight; cur != nil {
		if cur.pkg == rec.pkg && cur.version == rec.version {
			g.log.Debug("already fetching this artifact",
				"package", rec.pkg, "version", rec.version)
			return
		}
		// The device moved on to a different artifact, so this one is moot.
		g.log.Info("abandoning the previous artifact",
			"package", cur.pkg, "version", cur.version)
		g.downloads.cancel()
	}

	// Detached from the session context on purpose: the session that carried
	// the record ends long before the download does.
	dlCtx, cancel := context.WithCancel(ctx)
	g.downloads.inflight = rec
	g.downloads.cancel = cancel

	g.log.Info("fetching artifact on the device's behalf",
		"package", rec.pkg, "version", rec.version, "bytes", rec.size)

	go func() {
		res := g.Firmware.Downloader.Fetch(dlCtx, rec)
		cancel()

		g.downloads.mu.Lock()
		defer g.downloads.mu.Unlock()
		g.downloads.inflight = nil
		g.downloads.cancel = nil
		g.downloads.result = &res
	}()
}

// takeDownloadResult returns a finished download's verdict, once.
func (g *Gateway) takeDownloadResult() *downloadResult {
	g.downloads.mu.Lock()
	defer g.downloads.mu.Unlock()

	res := g.downloads.result
	g.downloads.result = nil
	return res
}

// stopDownloads cancels anything in flight, for shutdown.
func (g *Gateway) stopDownloads() {
	g.downloads.mu.Lock()
	defer g.downloads.mu.Unlock()

	if g.downloads.cancel != nil {
		g.downloads.cancel()
	}
}
