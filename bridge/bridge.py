"""
bridge.py — read Scale4 USB serial and forward to Supabase.

Reads lines like:
    c1=1380g  c2=1370g  c3=1390g  c4=1360g  | total = 5500 g
or:
    weight = 5500 g

For each parsed reading:
  - throttled to 1 Hz, UPDATEs the singleton row in `live`
  - runs a state machine (load -> stable -> capture) and INSERTs a row
    into `weighings` for each stable measurement event

Required env vars (set in .env next to this file):
    SERIAL_PORT          e.g. COM5 (Windows) or /dev/ttyACM0 (Linux)
    SERIAL_BAUD          57600
    SUPABASE_URL         from your Supabase project
    SUPABASE_SERVICE_KEY service-role key (server-side only, KEEP SECRET)
"""
import os
import re
import sys
import time
from dataclasses import dataclass
from datetime import datetime, timezone
from enum import Enum
from typing import Optional

import serial
from dotenv import load_dotenv
from supabase import Client, create_client

# ---------- config ----------
load_dotenv()
SERIAL_PORT = os.environ["SERIAL_PORT"]
SERIAL_BAUD = int(os.environ.get("SERIAL_BAUD", "57600"))
SUPABASE_URL = os.environ["SUPABASE_URL"]
SUPABASE_SERVICE_KEY = os.environ["SUPABASE_SERVICE_KEY"]

LIVE_UPDATE_PERIOD_S = 1.0     # throttle live UPDATE to this rate
LOAD_THRESHOLD_G     = 100.0   # above this = something is on it
UNLOAD_THRESHOLD_G   = 50.0    # below this = cleared
STABLE_BAND_G        = 10.0    # must stay inside this band
STABLE_TIME_S        = 1.5     # for this long, to count as stable

# ---------- parsing ----------
LINE_RE = re.compile(
    r"c1=(-?\d+)g\s+c2=(-?\d+)g\s+c3=(-?\d+)g\s+c4=(-?\d+)g\s*\|\s*total\s*=\s*(-?\d+)\s*g"
)
SIMPLE_RE = re.compile(r"weight\s*=\s*(-?\d+)\s*g")


@dataclass
class Reading:
    total_g: float
    c1_g: Optional[float] = None
    c2_g: Optional[float] = None
    c3_g: Optional[float] = None
    c4_g: Optional[float] = None


def parse_line(line: str) -> Optional[Reading]:
    m = LINE_RE.search(line)
    if m:
        c1, c2, c3, c4, total = (float(x) for x in m.groups())
        return Reading(total_g=total, c1_g=c1, c2_g=c2, c3_g=c3, c4_g=c4)
    m = SIMPLE_RE.search(line)
    if m:
        return Reading(total_g=float(m.group(1)))
    return None


# ---------- event detector ----------
class State(Enum):
    WAITING = 0
    LOADING = 1
    WEIGHED = 2


class EventDetector:
    def __init__(self) -> None:
        self.state = State.WAITING
        self.stable_ref = 0.0
        self.stable_since = 0.0

    def update(self, total_g: float, now: float) -> Optional[float]:
        """Returns captured grams when a weighing event fires, else None."""
        if self.state is State.WAITING:
            if abs(total_g) > LOAD_THRESHOLD_G:
                self.state = State.LOADING
                self.stable_ref = total_g
                self.stable_since = now
            return None

        if self.state is State.LOADING:
            if abs(total_g - self.stable_ref) > STABLE_BAND_G:
                self.stable_ref = total_g
                self.stable_since = now
            elif now - self.stable_since > STABLE_TIME_S:
                self.state = State.WEIGHED
                return total_g
            return None

        # WEIGHED
        if abs(total_g) < UNLOAD_THRESHOLD_G:
            self.state = State.WAITING
        return None


# ---------- main loop ----------
def main() -> None:
    print(f"Supabase: {SUPABASE_URL}")
    sb: Client = create_client(SUPABASE_URL, SUPABASE_SERVICE_KEY)
    detector = EventDetector()
    last_live = 0.0

    while True:
        try:
            print(f"Opening serial {SERIAL_PORT} @ {SERIAL_BAUD}...")
            ser = serial.Serial(SERIAL_PORT, SERIAL_BAUD, timeout=2)
            print("Connected. Reading...")
            while True:
                raw = ser.readline()
                if not raw:
                    continue
                line = raw.decode("utf-8", errors="replace").strip()
                reading = parse_line(line)
                if reading is None:
                    continue

                now = time.monotonic()

                # event capture
                captured = detector.update(reading.total_g, now)
                if captured is not None:
                    print(f"*** WEIGHING: {captured:.0f} g")
                    try:
                        sb.table("weighings").insert(
                            {"weight_g": float(captured)}
                        ).execute()
                    except Exception as exc:
                        print(f"  weighings insert failed: {exc}")

                # throttled live update
                if now - last_live >= LIVE_UPDATE_PERIOD_S:
                    last_live = now
                    payload = {
                        "weight_g": float(reading.total_g),
                        "updated_at": datetime.now(timezone.utc).isoformat(),
                    }
                    for k, v in (
                        ("c1_g", reading.c1_g),
                        ("c2_g", reading.c2_g),
                        ("c3_g", reading.c3_g),
                        ("c4_g", reading.c4_g),
                    ):
                        if v is not None:
                            payload[k] = float(v)
                    try:
                        sb.table("live").update(payload).eq("id", 1).execute()
                    except Exception as exc:
                        print(f"  live update failed: {exc}")

        except (serial.SerialException, OSError) as exc:
            print(f"Serial error: {exc} — reconnecting in 3 s...")
            time.sleep(3)
        except KeyboardInterrupt:
            print("Stopping.")
            sys.exit(0)


if __name__ == "__main__":
    main()
