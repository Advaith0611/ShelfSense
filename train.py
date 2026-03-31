import pandas as pd
import numpy as np
import joblib
import matplotlib.pyplot as plt

from sklearn.ensemble import RandomForestRegressor
from sklearn.metrics import r2_score

# 1. Load data
df = pd.read_csv("synthetic_inventory.csv")

# OPTIONAL: If you have a date column, uncomment this
# df["Date"] = pd.to_datetime(df["Date"])
# df = df.sort_values("Date")

# 2. Create time index
df["DayIndex"] = np.arange(len(df))

# 3. Feature Engineering (THIS IS THE KEY FIX)

# Lag features
df["lag_1"] = df["Widget_A"].shift(1)
df["lag_2"] = df["Widget_A"].shift(2)
df["lag_7"] = df["Widget_A"].shift(7)

# Rolling statistics
df["rolling_mean_7"] = df["Widget_A"].rolling(window=7).mean()
df["rolling_std_7"] = df["Widget_A"].rolling(window=7).std()

# Simple seasonality (weekly pattern)
df["day_of_week"] = df["DayIndex"] % 7

# Drop rows with NaNs from feature creation
df = df.dropna()

# 4. Define features and target
features = [
    "lag_1", "lag_2", "lag_7",
    "rolling_mean_7", "rolling_std_7",
    "day_of_week"
]

X = df[features]
y = df["Widget_A"]

# 5. Train/Test Split (time-aware, no shuffle)
split = int(len(df) * 0.8)

X_train, X_test = X.iloc[:split], X.iloc[split:]
y_train, y_test = y.iloc[:split], y.iloc[split:]

# 6. Train model (stronger than linear regression)
model = RandomForestRegressor(
    n_estimators=200,
    max_depth=10,
    random_state=42
)

model.fit(X_train, y_train)

# 7. Evaluate
y_pred = model.predict(X_test)

r2 = r2_score(y_test, y_pred)
print(f"Test R^2 score: {r2:.3f}")

# 8. Plot predictions vs actual
plt.figure(figsize=(10, 5))
plt.plot(y_test.values, label="Actual")
plt.plot(y_pred, label="Predicted")
plt.legend()
plt.title("Actual vs Predicted")
plt.show()

# 9. Save model
joblib.dump(model, "model.pkl")
print("Model saved to model.pkl")
