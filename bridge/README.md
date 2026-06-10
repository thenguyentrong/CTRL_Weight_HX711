# Bridge

Reads the Arduino's USB serial output and forwards it to Supabase:
- updates a single `live` row at ~1 Hz (current weight + per-cell)
- inserts one `weighings` row per stable measurement event

## Setup

1. Create a Supabase project, run `../supabase/schema.sql` in the SQL editor.
2. `cp .env.example .env` and fill in:
   - `SERIAL_PORT` — `COM5` on Windows, `/dev/ttyACM0` on Linux/Mac
   - `SUPABASE_URL` and `SUPABASE_SERVICE_KEY` from Project Settings → API
3. `pip install -r requirements.txt`
4. `python bridge.py`

You should see `*** WEIGHING: 5520 g` lines whenever you place something on
the platform.

## Run as a service (24/7)

**Windows (NSSM):**
```
nssm install CTRLWeightBridge "C:\Python311\python.exe" "C:\path\to\bridge.py"
nssm set CTRLWeightBridge AppDirectory "C:\path\to\bridge"
nssm start CTRLWeightBridge
```

**Linux (systemd):** drop a unit into `/etc/systemd/system/ctrl-weight.service`:
```
[Unit]
Description=CTRL Weight bridge
After=network.target

[Service]
WorkingDirectory=/home/pi/CTRL_Weight_HX711/bridge
ExecStart=/usr/bin/python3 bridge.py
Restart=always
User=pi

[Install]
WantedBy=multi-user.target
```
Then `sudo systemctl enable --now ctrl-weight`.
