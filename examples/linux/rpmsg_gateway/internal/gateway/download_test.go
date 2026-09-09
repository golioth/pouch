package gateway

import (
	"context"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"io"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func quietLogger() *slog.Logger {
	return slog.New(slog.NewTextHandler(io.Discard, nil))
}

// newTestDownloader serves body at the artifact URL and returns a Downloader
// that trusts the test server, plus the record describing what it claims to be.
func newTestDownloader(t *testing.T, body []byte, claimed []byte,
	handler http.HandlerFunc) (*Downloader, *fwURLRecord) {
	t.Helper()

	if handler == nil {
		handler = func(w http.ResponseWriter, r *http.Request) {
			_, _ = w.Write(body)
		}
	}
	srv := httptest.NewTLSServer(handler)
	t.Cleanup(srv.Close)

	roots := x509.NewCertPool()
	roots.AddCert(srv.Certificate())

	d := &Downloader{
		Client: &http.Client{
			Transport: &http.Transport{
				TLSClientConfig: &tls.Config{MinVersion: tls.VersionTLS12, RootCAs: roots},
			},
		},
		Dir:     t.TempDir(),
		Timeout: 10 * time.Second,
		Log:     quietLogger(),
	}

	rec := &fwURLRecord{
		pkg:     "rpmsg_device",
		version: "1.0.3",
		size:    uint32(len(claimed)),
		sha256:  sha256.Sum256(claimed),
		url:     srv.URL + "/.u/c/rpmsg_device@1.0.3",
	}
	return d, rec
}

func TestFetchVerifiedArtifact(t *testing.T) {
	image := []byte("the firmware image, such as it is")
	d, rec := newTestDownloader(t, image, image, nil)

	res := d.Fetch(context.Background(), rec)

	if res.verdict != FwStatusOK {
		t.Fatalf("verdict %v, want ok", res.verdict)
	}
	got, err := os.ReadFile(res.path)
	if err != nil {
		t.Fatalf("read installed artifact: %v", err)
	}
	if string(got) != string(image) {
		t.Errorf("installed artifact does not match what was served")
	}
	if filepath.Base(res.path) != "rpmsg_device-1.0.3.elf" {
		t.Errorf("installed as %q", filepath.Base(res.path))
	}
}

func TestFetchCorruptedArtifact(t *testing.T) {
	image := []byte("the firmware image, such as it is")
	corrupt := append([]byte(nil), image...)
	corrupt[0] ^= 0xff

	// Served corrupted, but the record still promises the original digest -
	// which is the whole point of carrying one.
	d, rec := newTestDownloader(t, corrupt, image, nil)

	res := d.Fetch(context.Background(), rec)

	if res.verdict != FwStatusHashFail {
		t.Fatalf("verdict %v, want hash-fail", res.verdict)
	}
	if entries, _ := os.ReadDir(d.Dir); len(entries) != 0 {
		t.Errorf("a failed download left %d files behind", len(entries))
	}
}

func TestFetchShortArtifact(t *testing.T) {
	image := []byte("the firmware image, such as it is")

	// Fewer bytes than promised, and no Content-Length to give it away.
	d, rec := newTestDownloader(t, nil, image, func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Transfer-Encoding", "chunked")
		_, _ = w.Write(image[:5])
	})

	res := d.Fetch(context.Background(), rec)

	if res.verdict != FwStatusError {
		t.Fatalf("verdict %v, want error", res.verdict)
	}
	if entries, _ := os.ReadDir(d.Dir); len(entries) != 0 {
		t.Errorf("a truncated download left %d files behind", len(entries))
	}
}

func TestFetchOverlongArtifact(t *testing.T) {
	image := []byte("the firmware image, such as it is")

	d, rec := newTestDownloader(t, nil, image, func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Transfer-Encoding", "chunked")
		_, _ = w.Write(append(append([]byte(nil), image...), "extra"...))
	})

	res := d.Fetch(context.Background(), rec)

	// The read is bounded one byte past the promised size, so too-long is seen
	// as too long rather than silently truncated into a matching digest.
	if res.verdict != FwStatusError {
		t.Fatalf("verdict %v, want error", res.verdict)
	}
}

func TestFetchRefusedURL(t *testing.T) {
	image := []byte("the firmware image")

	// What a project without signed URLs enabled, or a CA the cloud does not
	// know, actually looks like from here.
	d, rec := newTestDownloader(t, nil, image, func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusForbidden)
	})

	res := d.Fetch(context.Background(), rec)

	if res.verdict != FwStatusError {
		t.Fatalf("verdict %v, want error", res.verdict)
	}
}

func TestFetchContentLengthMismatch(t *testing.T) {
	image := []byte("the firmware image, such as it is")

	// The server is honest about a body that is not the promised size, so this
	// is caught before a byte is written.
	d, rec := newTestDownloader(t, nil, image, func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write(image[:10])
	})

	res := d.Fetch(context.Background(), rec)

	if res.verdict != FwStatusError {
		t.Fatalf("verdict %v, want error", res.verdict)
	}
	if entries, _ := os.ReadDir(d.Dir); len(entries) != 0 {
		t.Errorf("a rejected download left %d files behind", len(entries))
	}
}

func TestFetchCancelled(t *testing.T) {
	image := []byte("the firmware image, such as it is")

	d, rec := newTestDownloader(t, image, image, func(w http.ResponseWriter, r *http.Request) {
		<-r.Context().Done()
	})

	ctx, cancel := context.WithCancel(context.Background())
	cancel()

	res := d.Fetch(ctx, rec)

	if res.verdict != FwStatusError {
		t.Fatalf("verdict %v, want error", res.verdict)
	}
}

func TestNewHTTPClientRejectsBadCA(t *testing.T) {
	f := filepath.Join(t.TempDir(), "not-a-ca.pem")
	if err := os.WriteFile(f, []byte("definitely not PEM"), 0o644); err != nil {
		t.Fatal(err)
	}

	if _, err := NewHTTPClient(f); err == nil {
		t.Fatal("accepted a file with no certificates in it")
	}
	if _, err := NewHTTPClient(filepath.Join(t.TempDir(), "missing.pem")); err == nil {
		t.Fatal("accepted a CA bundle that does not exist")
	}
	if _, err := NewHTTPClient(""); err != nil {
		t.Fatalf("system roots should need no file: %v", err)
	}
}
