# ShelfSense

## Overview
**ShelfSense** is a hackathon demo that demonstrates a complete IoT → cloud intelligence workflow. The system pairs an **ESP32**-based inventory scanner and web UI with a separate local/cloud demo layer that uses **synthetic data** to illustrate training and prediction. The design goal was to keep the hardware stable and immutable during the demo while showing a convincing AI pipeline and polished UX.

**Key highlights**
- Hardware runs unchanged ESP32 firmware that serves a web UI and sends alerts.  
- Synthetic data generator produces realistic depletion and refill cycles.  
- Lightweight ML pipeline trains quickly and saves a model artifact.  
- Prediction script produces a 7‑day forecast CSV for demo and visualization.  
- Everything reproducible locally; no cloud accounts required for the demo.

---

## Final State Summary
**What the demo delivers**
- A working **ESP32 web UI** for scanning and managing inventory.  
- A **synthetic dataset** (`synthetic_inventory.csv`) that simulates stock depletion and monthly refills with noise.  
- A fast **training script** (`train.py`) that fits a model and writes `model.pkl`.  
- A **prediction script** (`test.py`) that loads `model.pkl` and writes `predictions.csv`.  
- Clear, repeatable demo flow for judges: hardware interaction → data generation → training → prediction → CSV/chart outputs.

**Design principles**
- **No firmware changes** required to demonstrate the AI layer.  
- **Speed over complexity**: models are intentionally lightweight so training and prediction finish instantly during a demo.  
- **Believability**: synthetic data follows realistic patterns (steady depletion, periodic refill, random noise).  
- **Reproducibility**: all scripts and commands are included so anyone can run the demo locally.

---

## Files and Contents

| File | Type | Purpose |
|---|---:|---|
| **TeamRed_InventoryHandler.ino** | Arduino sketch | ESP32 firmware: web server, inventory logic, Telegram integration |
| **indexPage** (embedded) | HTML/CSS/JS raw literal | Polished web UI served by the ESP32 |
| **synthetic_inventory.csv** | CSV dataset | Synthetic time series for multiple products |
| **generate_data.py** | Python script / notebook cell | Generates `synthetic_inventory.csv` (depletion + refill + noise) |
| **train.py** | Python script | Fast training script that saves `model.pkl` |
| **model.pkl** | Binary model | Serialized model artifact produced by `train.py` |
| **test.py** | Python script | Loads `model.pkl`, predicts next 7 days, writes `predictions.csv` |
| **predictions.csv** | CSV output | Forecasted stock levels for demo |
| **requirements.txt** | Text | Python dependencies for training and prediction |
| **README.md** | Markdown | This documentation file |

---

## Files Explained in Detail

### TeamRed_InventoryHandler.ino
**Purpose**  
ESP32 firmware that hosts the web UI and manages inventory state. It also contains endpoints used by the UI and can send Telegram alerts.

**Important parts**
- **Web endpoints**: `/` (UI), `/status` (JSON status), `/add` (add product), `/setmode`, `/setschedule`, `/manual` (manual adjustments).  
- **Embedded UI**: `indexPage` raw literal contains the full HTML/CSS/JS. Ensure the raw literal ends with `)rawliteral";` to avoid compilation errors.  
- **Inventory logic**: in-memory product list, thresholds, last-scan tracking, and audit log.  
- **Telegram**: `sendMessage` calls to the Bot API for threshold alerts. Remember to call `/start` in Telegram and use `getUpdates` to find the correct `chat_id` during setup.

---

### indexPage (embedded HTML)
**Purpose**  
Polished single-page UI served by the ESP32. Designed for clarity and demo polish.

**Features**
- **Mode toggle** (increment/decrement) with label.  
- **Last scan card** showing UID, known/unknown, product name, quantity, and message.  
- **Add product form** with labeled inputs: UID, product name, initial quantity, threshold, optional note.  
- **Inventory area** that receives HTML from the server (`inventoryHtml`).  
- **Schedule controls** with labeled inputs for frequency, weekday, hour, minute.  
- **Manual quick actions** to increment/decrement by a specified amount.  
- **Polling**: UI polls `/status` every 2 seconds to stay live.

---

### synthetic_inventory.csv
**Purpose**  
A reproducible dataset used to demonstrate training and prediction without connecting to live hardware or cloud services.

**Structure**
- `Date` column (ISO date)  
- `Widget_A`, `Widget_B`, `Widget_C` columns (stock levels per day)

**Generation rules**
- Start each product at **100 units** at the beginning of a 30‑day cycle.  
- **Deplete ~3 units per day** (deterministic decrement).  
- Add **random noise** each day (e.g., ±1–2 units) to make curves realistic.  
- **Refill to 100** at the start of each 30‑day cycle.  
- Simulate multiple cycles (e.g., 60 days) for a convincing demo.

---

### generate_data.py
**Purpose**  
Script or notebook cell that creates `synthetic_inventory.csv`. Use this to regenerate data with different seeds, durations, or depletion rates.

**Core logic (conceptual)**
```python
import pandas as pd
import numpy as np
import datetime

days = 60
dates = pd.date_range(datetime.date.today() - datetime.timedelta(days=days-1), periods=days)
products = ["Widget_A","Widget_B","Widget_C"]

data = {"Date": dates}
for p in products:
    stock = []
    current = 100
    for i in range(days):
        if i % 30 == 0:
            current = 100
        else:
            current = max(current - 3, 0)
        noise = np.random.randint(-2, 3)
        stock.append(max(current + noise, 0))
    data[p] = stock

df = pd.DataFrame(data)
df.to_csv("synthetic_inventory.csv", index=False)
```

---

### train.py
**Purpose**  
Train a fast, demonstrative model on the synthetic data and save it as `model.pkl`. The script is intentionally lightweight so it completes instantly during a demo.

**Behavior**
- Loads `synthetic_inventory.csv`.  
- Creates a `DayIndex` feature (0..N).  
- Trains `LinearRegression` on `Widget_A` vs `DayIndex`.  
- Prints a quick R² score and saves `model.pkl` using `joblib`.

**Example**
```python
import pandas as pd
import numpy as np
import joblib
from sklearn.linear_model import LinearRegression

df = pd.read_csv("synthetic_inventory.csv")
df["DayIndex"] = np.arange(len(df))
X = df[["DayIndex"]]
y = df["Widget_A"]

model = LinearRegression()
model.fit(X, y)
print("R^2:", model.score(X, y))
joblib.dump(model, "model.pkl")
```

**Notes**
- No train/test split to keep training time minimal.  
- Optionally add a short fake progress loop for demo flair:
```python
import time
for i in range(4):
    print(f"Training step {i+1}/4...")
    time.sleep(0.4)
```

---

### test.py
**Purpose**  
Load `model.pkl`, generate predictions for the next 7 days, print them, and save them to `predictions.csv`.

**Example**
```python
import pandas as pd
import numpy as np
import joblib
import datetime

model = joblib.load("model.pkl")
df = pd.read_csv("synthetic_inventory.csv")
last_index = len(df) - 1
future_indices = np.arange(last_index + 1, last_index + 8).reshape(-1, 1)
preds = model.predict(future_indices)

future_dates = pd.date_range(datetime.date.today() + datetime.timedelta(days=1), periods=7)
results = pd.DataFrame({"Date": future_dates, "Predicted_Widget_A": preds})
print(results)
results.to_csv("predictions.csv", index=False)
```

---

### predictions.csv
**Purpose**  
CSV output of `test.py` containing the 7‑day forecast. Useful for quick plotting or importing into a dashboard.

**Columns**
- `Date`  
- `Predicted_Widget_B`

---

### requirements.txt
**Suggested contents**
```
pandas
numpy
scikit-learn
joblib
matplotlib
```

**Purpose**  
Install dependencies quickly:
```bash
pip install -r requirements.txt
```

---

## Setup and Run Instructions

### Prerequisites
- Python 3.8+  
- pip  
- Arduino IDE or PlatformIO for ESP32 firmware upload (if you want to run the ESP32 part)

### Install Python dependencies
```bash
pip install -r requirements.txt
```

### Generate synthetic data
If you have `generate_data.py`:
```bash
python generate_data.py
```
This creates `synthetic_inventory.csv`.

### Train the model
```bash
python train.py
```
**Output**: `model.pkl`

### Predict next 7 days
```bash
python test.py
```
**Output**: `predictions.csv`

### Upload and run ESP32 firmware (optional)
1. Open `TeamRed_InventoryHandler.ino` in Arduino IDE.  
2. Select the correct ESP32 board and COM port.  
3. Update Wi‑Fi credentials and Telegram token/chat id in the sketch.  
4. Upload to the board.  
5. Open the device IP in a browser to access the web UI.

---

## Demo Flow and Talking Points

**Demo sequence**
1. **Hardware**: Show the ESP32 web UI, scan a tag, and demonstrate the last scan card updating.  
2. **Synthetic data**: Explain why synthetic data is used and open `synthetic_inventory.csv` to show the depletion/refill pattern.  
3. **Training**: Run `train.py` live. Emphasize speed and reproducibility.  
4. **Prediction**: Run `test.py` and show `predictions.csv`. Optionally plot results.  
5. **Wrap up**: Explain how the same pipeline can be connected to live Sheets or a cloud dashboard for production.

**Talking points**
- Separation of concerns: hardware remains stable while the cloud/demo layer evolves.  
- Realism: synthetic data mimics real usage patterns and allows controlled demos.  
- Speed: lightweight models keep the demo snappy.  
- Extensibility: the pipeline can be upgraded to time series models or connected to live data sources.

---

## Troubleshooting

**Common issues and fixes**

- **Arduino compile error after embedding HTML**  
  - Symptom: `error: 'function' does not name a type` or similar.  
  - Fix: Ensure the raw literal ends exactly with `)rawliteral";` on its own line after `</html>`.

- **No Telegram messages**  
  - Ensure you started the bot in Telegram by sending `/start`.  
  - Use `https://api.telegram.org/bot<YOUR_TOKEN>/getUpdates` to find the correct `chat_id`.  
  - Verify `TELEGRAM_TOKEN` and `TELEGRAM_CHAT_ID` in the sketch.

- **train.py seems slow**  
  - Use the lightweight linear regression version included here. It should run in under a second.

- **CSV not found**  
  - Confirm `synthetic_inventory.csv` is in the same directory as `train.py` and `test.py`.

- **Predictions look unrealistic**  
  - The linear model is intentionally simple. For more realistic forecasts, replace with a time series model (Prophet, ARIMA) and retrain.

---

## Development Log and Trial and Error Narrative

**Iteration highlights**

- **Initial prototype**  
  - Built a minimal ESP32 sketch with an embedded HTML UI. Hit a compilation error due to an unclosed raw literal. Fixed by adding `)rawliteral";`.

- **UI polish**  
  - Early UI lacked labels and clear inputs. Reworked the UI to add labels, helper text, and grouped cards for clarity.

- **Telegram integration**  
  - First tests failed because the bot had not been started and the `chat_id` was wrong. Resolved by using `getUpdates` and testing with `curl`.

- **Dashboard exploration**  
  - Looker Studio worked but felt unintuitive for a quick demo. Chose synthetic data + local scripts for speed and control.

- **Synthetic data tuning**  
  - Tuned noise and refill cadence to produce believable curves. Ensured CSV format is compatible with training and visualization.

- **Model selection**  
  - Initial heavy models were slow. Switched to linear regression for demo speed and reliability.

- **Final polish**  
  - Added clear README, demo flow, and troubleshooting notes so the demo can be reproduced quickly.

---

## Future Work and Extensions

**Short term**
- Add a simple Streamlit dashboard to visualize actual vs predicted values.  
- Train separate models per product and save multiple `model_*.pkl` artifacts.  
- Add a `predict_all.py` that forecasts for all products and writes a combined CSV.

**Medium term**
- Replace linear regression with a time series model (Prophet or ARIMA) for better forecasts.  
- Connect the pipeline to Google Sheets or a database for live ingestion.  
- Add automated alerts based on predicted depletion dates.

**Long term**
- Build a cloud service that retrains models periodically and exposes an API for predictions.  
- Integrate voice queries and team notifications (Slack, Teams).  
- Add role-based access and multi-user dashboards.

---

## License
**MIT License** — reuse and adapt freely for hackathon or educational purposes. Attribution appreciated for published derivatives.

---

## Contact
**Project lead**: Advaith
**Project name**: ShelfSense

---

**Quick copy commands**

```bash
# Install dependencies
pip install -r requirements.txt

# Generate data
python generate_data.py

# Train model
python train.py

# Predict next 7 days
python test.py
```

---

**Closing note**  
ShelfSense is intentionally modular: the ESP32 firmware, synthetic data generator, and ML demo are separate so you can iterate on the AI and UX without reflashing hardware. This makes the project ideal for hackathons where speed, polish, and reproducibility matter.