# train.py (lightweight version)
import pandas as pd
import numpy as np
import joblib
from sklearn.linear_model import LinearRegression

# 1. Load synthetic data
df = pd.read_csv("synthetic_inventory.csv")

# Use day index as feature
df["DayIndex"] = np.arange(len(df))

# Target: Widget_A stock
X = df[["DayIndex"]]
y = df["Widget_B"]

# 2. Train a simple linear regression (fast!)
model = LinearRegression()
model.fit(X, y)

# 3. Print quick evaluation
r2 = model.score(X, y)
print(f"Training complete. R^2 score: {r2:.3f}")

# 4. Save model
joblib.dump(model, "model.pkl")
print("Model saved to model.pkl")
