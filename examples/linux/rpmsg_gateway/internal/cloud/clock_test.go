package cloud

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"
)

// The server's clock is the authority on the timestamps it validates, so the
// client learns it from responses it is already making rather than trusting the
// local clock.
func TestClockOffsetFromDateHeader(t *testing.T) {
	ahead := 42 * time.Second

	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Date", time.Now().Add(ahead).UTC().Format(http.TimeFormat))
		_, _ = w.Write([]byte("chain"))
	}))
	defer srv.Close()

	c := New(srv.URL, 5*time.Second)

	if _, ok := c.ClockOffset(); ok {
		t.Fatal("reported an offset before any response was seen")
	}

	if _, err := c.ServerCert(context.Background()); err != nil {
		t.Fatalf("ServerCert: %v", err)
	}

	offset, ok := c.ClockOffset()
	if !ok {
		t.Fatal("no offset after a response carrying a Date header")
	}

	// Date has one-second resolution and the round trip adds a little, so the
	// estimate is good to about a second, not better.
	if d := offset - ahead; d > 2*time.Second || d < -2*time.Second {
		t.Errorf("offset %v, want about %v", offset, ahead)
	}
}

func TestClockOffsetFromErrorResponse(t *testing.T) {
	ahead := 30 * time.Second

	// A gateway that cannot authenticate yet still wants to learn the clock:
	// the header is on the refusal too.
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Date", time.Now().Add(ahead).UTC().Format(http.TimeFormat))
		w.WriteHeader(http.StatusUnauthorized)
	}))
	defer srv.Close()

	c := New(srv.URL, 5*time.Second)

	if _, err := c.ServerCert(context.Background()); err == nil {
		t.Fatal("a 401 should still be reported as an error")
	}

	offset, ok := c.ClockOffset()
	if !ok {
		t.Fatal("no offset learned from an error response")
	}
	if d := offset - ahead; d > 2*time.Second || d < -2*time.Second {
		t.Errorf("offset %v, want about %v", offset, ahead)
	}
}

func TestClockOffsetIgnoresUnusableDate(t *testing.T) {
	for _, tc := range []struct{ name, date string }{
		{"absent", ""},
		{"unparseable", "not a date"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				// Go sets Date itself unless it is explicitly suppressed.
				w.Header()["Date"] = nil
				if tc.date != "" {
					w.Header().Set("Date", tc.date)
				}
				_, _ = w.Write([]byte("chain"))
			}))
			defer srv.Close()

			c := New(srv.URL, 5*time.Second)
			if _, err := c.ServerCert(context.Background()); err != nil {
				t.Fatalf("ServerCert: %v", err)
			}

			if _, ok := c.ClockOffset(); ok {
				t.Error("claimed an offset from a response with no usable Date")
			}
		})
	}
}
