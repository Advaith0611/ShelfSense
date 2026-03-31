import pandas as pd
import numpy as np
import datetime
days = 200  # simulate 2 months for a nice curve
start_date = datetime.date.today() - datetime.timedelta(days=days)
dates = pd.date_range(start_date, periods=days)

products = ["Widget_A", "Widget_B", "Widget_C"]
data = {"Date": dates}

for product in products:
    stock = []
    current = 125
    for i in range(days):
        # Every 30 days, reset to 100
        val = np.random.randint(26, 34)
        if i % val == 0:
            current = 125
        else:
            # Deplete ~3 units per day
            current -= 3
            if current < 0:
                current = 0
        # Add random noise ±2
        noise = np.random.randint(-2, 3)
        val = max(current + noise, 0)
        stock.append(val)
    data[product] = stock

df = pd.DataFrame(data)
print(df.head(10))
df.to_csv("synthetic_inventory.csv", index=False)

import matplotlib.pyplot as plt

plt.figure(figsize=(10,6))
for product in products:
    plt.plot(df["Date"], df[product], label=product)
plt.legend()
plt.title("Synthetic Inventory Simulation")
plt.xlabel("Date")
plt.ylabel("Stock Level")
plt.show()
