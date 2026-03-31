# predict.py
import pandas as pd
import numpy as np
import joblib
import datetime

# 1. Load the trained model
model = joblib.load("model.pkl")

# 2. Load the original data (to know how many days we already simulated)
df = pd.read_csv("synthetic_inventory.csv")
last_day_index = len(df) - 1

# 3. Create future day indices (next 7 days)
future_indices = np.arange(last_day_index + 1, last_day_index + 8).reshape(-1, 1)

# 4. Predict stock levels for Widget_A
predictions = model.predict(future_indices)

# 5. Build a results DataFrame
future_dates = pd.date_range(
    datetime.date.today() + datetime.timedelta(days=1), periods=7
)
results = pd.DataFrame({
    "Date": future_dates,
    "Predicted_Widget_A": predictions
})

# 6. Print results
print("Forecast for Widget_A (next 7 days):")
print(results)

# 7. Save to CSV
results.to_csv("predictions.csv", index=False)
print("Predictions saved to predictions.csv")
